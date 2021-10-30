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
struct dhmp_client *client_mgr=NULL;
struct dhmp_send_mr * init_read_mr(int buffer_size, struct ibv_pd* pd);
struct dhmp_device *dhmp_get_dev_from_client();


struct dhmp_device *dhmp_get_dev_from_client()
{
	struct dhmp_device *res_dev_ptr=NULL;
	if(!list_empty(&client_mgr->dev_list))
	{
		res_dev_ptr=list_first_entry(&client_mgr->dev_list,
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
		conn = dhmp_transport_create(&client_mgr->ctx, 
								dhmp_get_dev_from_client(),		/* device 这里需要考虑下多个节点的情况吗？ 我认为不用，因为一个node只有一个RDMA设备*/
								false,
								false);
		if(!conn)
		{
			ERROR_LOG("create the [%d]-th transport error.", node_id);
			return NULL;
		}

		dhmp_transport_connect(conn,
								client_mgr->config.net_infos[node_id].addr,
								client_mgr->config.net_infos[node_id].port);

		/* main thread sleep a while, wait dhmp_event_channel_handler finish connection*/
		sleep(1);

		if(conn->trans_state < DHMP_TRANSPORT_STATE_CONNECTED)
			continue;
		else if(conn->trans_state == DHMP_TRANSPORT_STATE_REJECT)
		{
			free_trans(conn);
			free(conn);
		}
		else if(conn->trans_state == DHMP_TRANSPORT_STATE_ADDR_ERROR)
		{
			free_trans(conn);
			free(conn);
		}
		else if(conn->trans_state == DHMP_TRANSPORT_STATE_CONNECTED )
		{
			conn->node_id = node_id;
			conn->is_server = false;
			break;
		}
	}
	
	DEBUG_LOG("CONNECT finished: Peer Server %d has been connnected!", node_id);
	return conn;
}

struct dhmp_client *  dhmp_client_init(size_t buffer_size, int server_id, int node_class)
{
	int i;
	int re = 0;
	struct dhmp_device * cli_pd;

	client_mgr=(struct dhmp_client *)malloc(sizeof(struct dhmp_client));
	memset(client_mgr, 0 , sizeof(struct dhmp_client));

	if(!client_mgr)
	{
		ERROR_LOG("alloc memory error.");
		return NULL;
	}

	// dhmp_config_init(&client_mgr->config, true);

	// 我们这里直接使用 server 创建的 config 结构体，而不是自己去初始化
	memcpy(&client_mgr->config, &server_instance->config, sizeof(struct dhmp_config));

	re = dhmp_context_init(&client_mgr->ctx);

	/*init list about rdma device*/
	INIT_LIST_HEAD(&client_mgr->dev_list);
	dhmp_dev_list_init(&client_mgr->dev_list);

	/*init FIFO node select algorithm*/
	client_mgr->fifo_node_index=0;

	/*init the addr hash table of client_mgr*/
	for(i=0;i<DHMP_CLIENT_HT_SIZE;i++)
	{
		INIT_HLIST_HEAD(&client_mgr->addr_info_ht[i]);
	}

	/*init the structure about send mr list */
	pthread_mutex_init(&client_mgr->mutex_send_mr_list, NULL);
	INIT_LIST_HEAD(&client_mgr->send_mr_list);

	/*init normal connection*/
	memset(client_mgr->connect_trans, 0, DHMP_SERVER_NODE_NUM*
										sizeof(struct dhmp_transport*));
	
	if(node_class == HEAD)
	{
		// 头节点需要主动和所有的node建立rdma连接，所有的node都是头节点的server
		for(i=0; i<client_mgr->config.nets_cnt; i++)
		{
			/*server_instance skip himself to avoid connecting himself*/
			if(server_id == i)
			{
				client_mgr->node_id = i;
				client_mgr->connect_trans[i] = NULL;
				continue;
			}

			INFO_LOG("CONNECT BEGIN: create the [%d]-th normal transport.",i);
			client_mgr->connect_trans[i] = dhmp_connect(i);
			if(!client_mgr->connect_trans[i])
			{
				ERROR_LOG("create the [%d]-th transport error.",i);
				continue;
			}
			client_mgr->connect_trans[i]->is_server = false;
			client_mgr->connect_trans[i]->node_id = i;
			client_mgr->read_mr[i] = init_read_mr(buffer_size, client_mgr->connect_trans[i]->device->pd);
		}
	}
	else if(node_class == NORMAL)
	{
		// 中间节点需要主动和下游节点建立rdma连接，只有下游节点是中间节点的server
		int next_id = server_id+1;
		client_mgr->connect_trans[next_id] = dhmp_connect(next_id);

		if(!client_mgr->connect_trans[next_id]){
			ERROR_LOG("create the [%d]-th transport error.",next_id);
			exit(0);
		}

		client_mgr->connect_trans[next_id]->is_server = false;
		client_mgr->connect_trans[next_id]->node_id = next_id;
		client_mgr->read_mr[next_id] = init_read_mr(buffer_size, client_mgr->connect_trans[next_id]->device->pd);	
	}
	else if(node_class == TAIL)
	{
		// 尾部节点不需要主动和任何节点建立rdma连接，没有节点是尾部节点的server
	}

	/* 初始化client段全局对象 */
	// global_verbs_send_mr = (struct dhmp_send_mr* )malloc(sizeof(struct dhmp_send_mr));

	/*init the structure about work thread*/
	pthread_mutex_init(&client_mgr->mutex_work_list, NULL);
	pthread_mutex_init(&client_mgr->mutex_asyn_work_list, NULL);
	INIT_LIST_HEAD(&client_mgr->work_list);
	INIT_LIST_HEAD(&client_mgr->work_asyn_list);

	pthread_create(&client_mgr->work_thread1, NULL, dhmp_work_handle_thread, (void*)client_mgr);
	pthread_create(&client_mgr->work_thread2, NULL, dhmp_work_handle_thread, (void*)client_mgr);
	pthread_create(&client_mgr->work_thread3, NULL, dhmp_work_handle_thread, (void*)client_mgr);

	return client_mgr;
}



struct dhmp_server * dhmp_server_init()
{
	int i,err=0;
	memset((void*)used_id, -1, sizeof(int) * MAX_PORT_NUMS);
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

	while (1)
	{
		err=dhmp_transport_listen(server_instance->listen_trans,
				server_instance->config.net_infos[server_instance->config.curnet_id].port);

		if (err == 0)
		{
			INFO_LOG("Final curnet_id is %d, port is %u", server_instance->config.curnet_id, \
					(unsigned int)server_instance->config.net_infos[server_instance->config.curnet_id].port);
			
			server_instance->server_id = server_instance->config.curnet_id;
			MID_LOG("Server's node id is [%d], num_chain_clusters is [%d]", \
					server_instance->server_id, server_instance->num_chain_clusters);
			break;
		}
		else
		{
			used_id[used_nums++] = server_instance->config.curnet_id;
			dhmp_set_curnode_id ( &server_instance->config );
		}
	}

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


