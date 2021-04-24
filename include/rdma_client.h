#ifndef RDMA_CLIENT_H
#define RDMA_CLIENT_H

struct dhmp_client{
	struct dhmp_context ctx;
	struct dhmp_config config;

	struct list_head dev_list;

	struct dhmp_transport *connect_trans[DHMP_SERVER_NODE_NUM];
	struct dhmp_transport *poll_trans[DHMP_SERVER_NODE_NUM];

	/*store the dhmp_addr_entry hashtable*/
	//pthread_mutex_t mutex_ht;
	struct hlist_head addr_info_ht[DHMP_CLIENT_HT_SIZE];

	int poll_ht_fd;
	struct timespec poll_interval;
	pthread_t poll_ht_thread;
	
	pthread_mutex_t mutex_send_mr_list;
	struct list_head send_mr_list;

	/*use for node select*/
	int fifo_node_index;

	pthread_t work_thread;
	pthread_mutex_t mutex_work_list;
	struct list_head work_list;

	/*per cycle*/
	int access_total_num;
	size_t access_region_size;
	size_t pre_average_size;

	/*use for countint the num of sending server poll's packets*/
	int poll_num;
	

	struct dhmp_send_mr* read_mr[DHMP_SERVER_NODE_NUM];
};

#endif