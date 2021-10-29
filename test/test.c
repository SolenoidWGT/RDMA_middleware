/*
 * @Descripttion: 
 * @version: 
 * @Author: sueRimn
 * @Date: 2021-04-25 17:46:17
 * @LastEditors: Please set LastEditors
 * @LastEditTime: 2021-05-18 20:30:56
 */
#include <stdio.h>
#include <sys/time.h>
#include "dhmp.h"
#include "dhmp_log.h"
#include "dhmp_hash.h"
#include "dhmp_config.h"
#include "dhmp_context.h"
#include "dhmp_dev.h"
#include "dhmp_transport.h"
#include "dhmp_task.h"
#include "dhmp_work.h"
#include "dhmp_client.h"
#include "dhmp_server.h"
#include "dhmp_init.h"
#include "mid_rdma_utils.h"

#include "log_copy.h"

#define COUNT 100000
#define SIZE 1024*8
#define WAIT_TIME 5
//unsigned long get_mr_time = 0;


int main(int argc,char *argv[])
{
	pthread_t writerForRemote, readerForLocal, nic_thead;
	struct dhmp_server * mid_server;

	/* 
	 * Init buffer mate lock must at the very beginning, before dhmp_server_init, 
	 * this node will not recv any request from other node. 
	 */ 
	pthread_mutex_init(&buff_init_lock, NULL);

	/*init list about rdma device*/
	mid_server = dhmp_server_init();
	/* wait peer server_instance init server_instance*/
	MID_LOG("Test begin!");

    if(server_instance->num_chain_clusters <= 1)
    {
        ERROR_LOG("num_chain_clusters is less than 2, exit!");
        exit(0);
    }

    if(server_instance->server_id == server_instance->num_chain_clusters - 1)
		node_class = TAIL; 		// 尾节点
    else if(server_instance->server_id == 0)
        node_class = HEAD;		// 头节点
    else
        node_class = NORMAL; 	// 中间节点

	MID_LOG("Node [%d] server_instance has finished init itself's server_instance, \
					begin establish connections with other nodes", mid_server->server_id);

	dhmp_client_init(SIZE*2, mid_server->server_id, node_class);

	MID_LOG("Node [%d] establish connection with other nodes", mid_server->server_id);
	// MID_LOG("server_instance [%d] has extablished connecting with node [%d]", \
	// 					mid_server->server_id, find_next_node(server_instance->server_id));

	buff_init();

    if (node_class == HEAD)
    {
        // 头节点
        pthread_create(&writerForRemote, NULL, writer_thread, NULL);
        // pthread_create(&nic_thead, NULL, NIC_thread, NULL);
	}
	if (node_class == NORMAL)
	{
		pthread_create(&readerForLocal, NULL, reader_thread, NULL);
		pthread_create(&nic_thead, NULL, NIC_thread, NULL);
		MID_LOG("NORMAL node[%d] init sucess!", server_instance->server_id);
	}
	else
	{
		pthread_create(&readerForLocal, NULL, reader_thread, NULL);
		MID_LOG("TAIL node[%d] init sucess!", server_instance->server_id);
	}

	pthread_join(server_instance->ctx.epoll_thread, NULL);
	return 0;
}
