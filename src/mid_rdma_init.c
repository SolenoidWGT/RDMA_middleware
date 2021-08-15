#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <arpa/inet.h>

#include "../include/dhmp.h"
#include "../include/dhmp_transport.h"
#include "../include/dhmp_server.h"
#include "../include/dhmp_dev.h"
#include "../include/dhmp_client.h"
#include "../include/dhmp_log.h"

struct dhmp_server *server_instance=NULL;
struct dhmp_client *midd_client=NULL;
struct dhmp_send_mr * init_read_mr(int buffer_size, struct ibv_pd* pd);
struct dhmp_device *dhmp_get_dev_from_client();


struct dhmp_device *dhmp_get_dev_from_client()
{
	struct dhmp_device *res_dev_ptr=NULL;
	if(!list_empty(&midd_client->dev_list))
	{
		res_dev_ptr=list_first_entry(&midd_client->dev_list,
									struct dhmp_device,
									dev_entry);
	}
		
	return res_dev_ptr;
}

/**
 *	dhmp_get_dev_from_server:get the dev_ptr from dev_list of server_instance.
 */
struct dhmp_device *dhmp_get_dev_from_server()
{
	struct dhmp_device *res_dev_ptr=NULL;
	if(!list_empty(&server_instance->dev_list))
	{
		res_dev_ptr=list_first_entry(&server_instance->dev_list,
									struct dhmp_device,
									dev_entry);
	}
		
	return res_dev_ptr;
}

struct dhmp_send_mr * 
init_read_mr(int buffer_size, struct ibv_pd* pd)
{
	struct dhmp_send_mr * rd_mr = malloc(sizeof(struct dhmp_send_mr));
	void* tmp_buf = malloc(buffer_size);

	memset(tmp_buf, 0, buffer_size);
	rd_mr->mr = ibv_reg_mr(pd, tmp_buf, buffer_size,
										IBV_ACCESS_LOCAL_WRITE);
	return rd_mr;
}


struct dhmp_transport * 
dhmp_connect(int node_id)
{
	struct dhmp_transport * conn = NULL;
	INFO_LOG("create the [%d]-th normal transport.",node_id);

	while(1)
	{
		conn = dhmp_transport_create(&midd_client->ctx, 
								dhmp_get_dev_from_client(),		/* device 这里需要考虑下多个节点的情况吗？ 我认为不用，因为一个node只有一个RDMA设备*/
								false,
								false);
		if(!conn)
		{
			ERROR_LOG("create the [%d]-th transport error.", node_id);
			return NULL;
		}

		dhmp_transport_connect(conn,
								midd_client->config.net_infos[node_id].addr,
								midd_client->config.net_infos[node_id].port);

		/* main thread sleep a while, wait dhmp_event_channel_handler finish connection*/
		sleep(1);

		if(conn->trans_state < DHMP_TRANSPORT_STATE_CONNECTED)
			continue;
		else if(conn->trans_state == DHMP_TRANSPORT_STATE_REJECT)
		{
			free_trans(conn);
			free(conn);
			conn=dhmp_transport_create(&midd_client->ctx, 
											dhmp_get_dev_from_client(),
											false,
											false);
			if(!conn)
			{
				ERROR_LOG("create the [%d]-th transport error.",node_id);
				continue;
			}
			
			conn->node_id = node_id;
			conn->is_server = false;
		}
		else if(conn->trans_state == DHMP_TRANSPORT_STATE_CONNECTED )
			break;
	}
	
	DEBUG_LOG("CONNECT END: Peer Server %d has been connnected!", node_id);
	return conn;
}

struct dhmp_client *  dhmp_client_init(size_t buffer_size, int server_id, int node_class)
{
	int i;
	int re = 0;
	struct dhmp_device * cli_pd;

	midd_client=(struct dhmp_client *)malloc(sizeof(struct dhmp_client));
	if(!midd_client)
	{
		ERROR_LOG("alloc memory error.");
		return NULL;
	}

	dhmp_config_init(&midd_client->config, true);
	re = dhmp_context_init(&midd_client->ctx);

	/*init list about rdma device*/
	INIT_LIST_HEAD(&midd_client->dev_list);
	dhmp_dev_list_init(&midd_client->dev_list);

	/*init FIFO node select algorithm*/
	midd_client->fifo_node_index=0;

	/*init the addr hash table of client*/
	for(i=0;i<DHMP_CLIENT_HT_SIZE;i++)
	{
		INIT_HLIST_HEAD(&midd_client->addr_info_ht[i]);
	}

	
	/*init the structure about send mr list */
	pthread_mutex_init(&midd_client->mutex_send_mr_list, NULL);
	INIT_LIST_HEAD(&midd_client->send_mr_list);

	
	/*init normal connection*/
	memset(midd_client->connect_trans, 0, DHMP_SERVER_NODE_NUM*
										sizeof(struct dhmp_transport*));
	
	if(node_class == HEAD)
	{
		// 头节点需要主动和所有的node建立rdma连接，所有的node都是头节点的server
		for(i=0; i<midd_client->config.nets_cnt; i++)
		{
			/*server_instance skip himself to avoid connecting himself*/
			if(server_id == i)
			{
				midd_client->node_id = i;
				midd_client->connect_trans[i] = NULL;
				continue;
			}

			INFO_LOG("CONNECT BEGIN: create the [%d]-th normal transport.",i);
			midd_client->connect_trans[i] = dhmp_connect(i);
			if(!midd_client->connect_trans[i])
			{
				ERROR_LOG("create the [%d]-th transport error.",i);
				continue;
			}
			midd_client->connect_trans[i]->is_server = false;
			midd_client->connect_trans[i]->node_id = i;
			midd_client->read_mr[i] = init_read_mr(buffer_size, midd_client->connect_trans[i]->device->pd);
		}
	}
	else if(node_class == NORMAL)
	{
		// 中间节点需要主动和下游节点建立rdma连接，只有下游节点是中间节点的server
		int next_id = server_id+1;
		midd_client->connect_trans[next_id] = dhmp_connect(next_id);

		if(!midd_client->connect_trans[next_id]){
			ERROR_LOG("create the [%d]-th transport error.",next_id);
			exit(0);
		}

		midd_client->connect_trans[next_id]->is_server = false;
		midd_client->connect_trans[next_id]->node_id = next_id;
		midd_client->read_mr[next_id] = init_read_mr(buffer_size, midd_client->connect_trans[next_id]->device->pd);	
	}
	else if(node_class == TAIL)
	{
		// 尾部节点不需要主动和任何节点建立rdma连接，没有节点是尾部节点的server
	}

	/* 初始化client段全局对象 */
	// global_verbs_send_mr = (struct dhmp_send_mr* )malloc(sizeof(struct dhmp_send_mr));

	/*init the structure about work thread*/
	pthread_mutex_init(&midd_client->mutex_work_list, NULL);
	INIT_LIST_HEAD(&midd_client->work_list);
	pthread_create(&midd_client->work_thread, NULL, dhmp_work_handle_thread, (void*)midd_client);
	return midd_client;
}



struct dhmp_server * dhmp_server_init()
{
	int i,err=0;

	server_instance=(struct dhmp_server *)malloc(sizeof(struct dhmp_server));
	if(!server_instance)
	{
		ERROR_LOG("allocate memory error.");
		return NULL;
	}
	
	dhmp_hash_init();
	dhmp_config_init(&server_instance->config, false);
	dhmp_context_init(&server_instance->ctx);
	server_instance->server_id = server_instance->config.curnet_id;
	server_instance->num_chain_clusters = server_instance->config.nets_cnt;
	MID_LOG("Server's node id is [%d], num_chain_clusters is [%d]", \
					server_instance->server_id, server_instance->num_chain_clusters);

	/*init client transport list*/
	server_instance->cur_connections=0;
	pthread_mutex_init(&server_instance->mutex_client_list, NULL);
	INIT_LIST_HEAD(&server_instance->client_list);

	/*init list about rdma device*/
	INIT_LIST_HEAD(&server_instance->dev_list);
	dhmp_dev_list_init(&server_instance->dev_list);


	server_instance->listen_trans=dhmp_transport_create(&server_instance->ctx,
											dhmp_get_dev_from_server(),
											true, false);
	if(!server_instance->listen_trans)
	{
		ERROR_LOG("create rdma transport error.");
		exit(-1);
	}
	err=dhmp_transport_listen(server_instance->listen_trans,
					server_instance->config.net_infos[server_instance->config.curnet_id].port);
	if(err)
		exit(- 1);
	return server_instance;
}


void dhmp_server_destroy()
{
	INFO_LOG("server_instance destroy start.");
	pthread_join(server_instance->ctx.epoll_thread, NULL);
	int err = 0;
	// err = memkind_destroy_kind(pmem_kind);
    //     if(err)
    //     {
    //             ERROR_LOG("memkind_destroy_kind() error.");
    //             return;
    //     }
		
	INFO_LOG("server_instance destroy end.");
	free(server_instance);
}


