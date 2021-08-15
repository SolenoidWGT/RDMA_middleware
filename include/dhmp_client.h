/*
 * @Descripttion: 
 * @version: 
 * @Author: sueRimn
 * @Date: 2021-04-25 17:46:17
 * @LastEditors: sueRimn
 * @LastEditTime: 2021-05-06 21:00:13
 */
#ifndef DHMP_CLIENT_H
#define DHMP_CLIENT_H

#include "dhmp_context.h"
#include "dhmp_config.h"
#define DHMP_CLIENT_HT_SIZE 50000

struct dhmp_client{
	/* data */
	int node_id;
	struct dhmp_context ctx;
	struct dhmp_config config;
	struct list_head dev_list;

	struct dhmp_transport *connect_trans[DHMP_SERVER_NODE_NUM];
	/*store the dhmp_addr_entry hashtable*/

	//pthread_mutex_t mutex_ht;
	struct hlist_head addr_info_ht[DHMP_CLIENT_HT_SIZE];
	pthread_mutex_t mutex_send_mr_list;
	struct list_head send_mr_list;
	
	int fifo_node_index;	/*use for node select*/

	pthread_t work_thread;
	pthread_mutex_t mutex_work_list;
	struct list_head work_list;

	struct dhmp_send_mr* read_mr[DHMP_SERVER_NODE_NUM];
};

extern struct dhmp_client *midd_client;
#endif


