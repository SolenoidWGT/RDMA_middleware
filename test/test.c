/*
 * @Descripttion: 
 * @version: 
 * @Author: sueRimn
 * @Date: 2021-04-25 17:46:17
 * @LastEditors: sueRimn
 * @LastEditTime: 2021-05-07 14:44:12
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
	struct dhmp_server * mid_server;

	/*init list about rdma device*/
	mid_server = dhmp_server_init();
	/* wait peer server init server*/
	int i;
	for(i =0; i<WAIT_TIME; i++){
		INFO_LOG("Please wait peer server init, left time is %d s", WAIT_TIME-i);
		sleep(1);
	}
	INFO_LOG("Node [%d] server has finished init", mid_server->server_id);

	dhmp_client_init(SIZE*2, mid_server->server_id);

	INFO_LOG("server [%d] has extablished connecting with node [%d]", mid_server->server_id, find_next_node(server->server_id));

	/* wait connect establish*/
	for(i =0; i<WAIT_TIME; i++){
		INFO_LOG("Please wait connect establish, left time is %d s", WAIT_TIME-i);
		sleep(1);
	}
	
	// char * base = (char * )malloc(SIZE * sizeof(char));
	// void* remote_addr = dhmp_malloc(SIZE, client_find_server_id());
	// dhmp_send(remote_addr, base, SIZE, true);
	// dhmp_send(remote_addr, base, SIZE, false);
	buff_init();



	dhmp_server_destroy();
	return 0;
}
