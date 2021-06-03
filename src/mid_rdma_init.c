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
struct dhmp_client *client=NULL;

struct dhmp_device *dhmp_get_dev_from_client()
{
	struct dhmp_device *res_dev_ptr=NULL;
	if(!list_empty(&client->dev_list))
	{
		res_dev_ptr=list_first_entry(&client->dev_list,
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


struct dhmp_client *  dhmp_client_init(size_t buffer_size, int server_id)
{
	int i;
	int re = 0;
	client=(struct dhmp_client *)malloc(sizeof(struct dhmp_client));
	if(!client)
	{
		ERROR_LOG("alloc memory error.");
		return NULL;
	}

	dhmp_config_init(&client->config, true);
	re = dhmp_context_init(&client->ctx);

	/*init list about rdma device*/
	INIT_LIST_HEAD(&client->dev_list);
	dhmp_dev_list_init(&client->dev_list);

	/*init FIFO node select algorithm*/
	client->fifo_node_index=0;

	/*init the addr hash table of client*/
	for(i=0;i<DHMP_CLIENT_HT_SIZE;i++)
	{
		INIT_HLIST_HEAD(&client->addr_info_ht[i]);
	}

	
	/*init the structure about send mr list */
	pthread_mutex_init(&client->mutex_send_mr_list, NULL);
	INIT_LIST_HEAD(&client->send_mr_list);

	
	/*init normal connection*/
	memset(client->connect_trans, 0, DHMP_SERVER_NODE_NUM*
										sizeof(struct dhmp_transport*));
	for(i=0; i<client->config.nets_cnt; i++)
	{
		/*server_instance skip himself to avoid connecting himself*/
		if(server_id == i)
		{
			client->node_id = i;
			client->connect_trans[i] = NULL;
			INFO_LOG("This node id is %d",	i);
			continue;
		}
		INFO_LOG("create the [%d]-th normal transport.",i);

		client->connect_trans[i] = dhmp_transport_create(&client->ctx, 
														dhmp_get_dev_from_client(),
														false,
														false);
		

		client->connect_trans[i]->is_server = false;

		if(!client->connect_trans[i])
		{
			ERROR_LOG("create the [%d]-th transport error.",i);
			continue;
		}
		client->connect_trans[i]->node_id = i;
		re = dhmp_transport_connect(client->connect_trans[i],
							client->config.net_infos[i].addr,
							client->config.net_infos[i].port);
	}

	for(i=0; i<client->config.nets_cnt; i++)
	{
		if(client->connect_trans[i]==NULL)
			continue;
		while(1)
		{
			if(client->connect_trans[i]->trans_state < DHMP_TRANSPORT_STATE_CONNECTED)
				continue;
			else if(client->connect_trans[i]->trans_state == DHMP_TRANSPORT_STATE_REJECT)
			{
				free_trans(client->connect_trans[i]);
				free(client->connect_trans[i]);
				client->connect_trans[i]=dhmp_transport_create(&client->ctx, 
												dhmp_get_dev_from_client(),
												false,
												false);
				if(!client->connect_trans[i])
				{
					ERROR_LOG("create the [%d]-th transport error.",i);
					continue;
				}
				
				client->connect_trans[i]->node_id = i;
				client->connect_trans[i]->is_server = false;

				re = dhmp_transport_connect(client->connect_trans[i],
									client->config.net_infos[i].addr,
									client->config.net_infos[i].port);
				/* main thread sleep a while, wait dhmp_event_channel_handler finish connection*/
				sleep(1);
			}
			else if(client->connect_trans[i]->trans_state == DHMP_TRANSPORT_STATE_CONNECTED )
				break;
		}
		DEBUG_LOG("Peer Server %d has been connnected!", i);
		client->read_mr[i] = malloc(sizeof(struct dhmp_send_mr));
		void* tmp_buf = malloc(buffer_size);
		memset(tmp_buf, 0, buffer_size);
		struct ibv_pd* pd = client->connect_trans[i]->device->pd;
		client->read_mr[i]->mr = ibv_reg_mr(pd, tmp_buf, buffer_size,
										 IBV_ACCESS_LOCAL_WRITE);
	}	
	
	/*init the structure about work thread*/
	pthread_mutex_init(&client->mutex_work_list, NULL);
	INIT_LIST_HEAD(&client->work_list);
	pthread_create(&client->work_thread, NULL, dhmp_work_handle_thread, (void*)client);
	return client;
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


