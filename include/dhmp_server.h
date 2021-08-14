#ifndef DHMP_SERVER_H
#define DHMP_SERVER_H
/*decide the buddy system's order*/
#define MAX_ORDER 5
#define SINGLE_AREA_SIZE 2097152

#define DHMP_DRAM_HT_SIZE 251

#include "dhmp_context.h"
#include "dhmp_config.h"
#include "dhmp.h"
#include "dhmp_transport.h"
#include "dhmp_server.h"
#include "dhmp_dev.h"


struct dhmp_server{
	/* data */
	int server_id;
	struct dhmp_context ctx;
	struct dhmp_config config;

	struct dhmp_transport *listen_trans;

	struct list_head dev_list;

	pthread_mutex_t mutex_client_list;
	struct list_head client_list;

	int cur_connections;		// 当前客户端连接数量
	int num_chain_clusters;		// 链式复制模式下集群的数量
	/* midderware*/
	// struct dhmp_area * recv_area;
	// struct dhmp_area * send_area;
};

extern struct dhmp_server *server_instance;

struct dhmp_device *dhmp_get_dev_from_server();


#endif


