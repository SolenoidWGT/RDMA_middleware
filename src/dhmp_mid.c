#include "dhmp.h"
#include "dhmp_log.h"
#include "dhmp_hash.h"
#include "dhmp_config.h"
#include "dhmp_context.h"
#include "dhmp_dev.h"
#include "dhmp_transport.h"
#include "dhmp_task.h"
#include "dhmp_timerfd.h"
#include "dhmp_poll.h"
#include "dhmp_work.h"
#include "dhmp_watcher.h"
#include "dhmp_client.h"
#include "dhmp_server.h"

static int err = 0;



void dhmp_midd_init()
{
	struct dhmp_server * server = dhmp_server_init();
	dhmp_client_init(1024, server->server_id);
}


void midd_init_server()
{ 
	int i, re;
    server=(struct dhmp_server *)malloc(sizeof(struct dhmp_server));
    if(!server)
	{
		ERROR_LOG("allocate memory error.");
		return ;
	}
    // 初始化链式复制结构
    dhmp_config_init(&server->config, false);		// false表示式服务端
    // 启动context_run线程，监听文件描述符
    dhmp_context_init(&server->ctx);

    /*初始化链中其他节点的连接*/
	server->cur_connections=0;
	pthread_mutex_init(&server->mutex_client_list, NULL);
	INIT_LIST_HEAD(&server->client_list);

    /*暂时先使用内存池管理缓冲区*/
    server->dram_total_size=numa_node_size(server->config.mem_infos->dram_node, NULL);
	server->nvm_total_size=numa_node_size(server->config.mem_infos->nvm_node, NULL);
	server->dram_used_size=server->nvm_used_size=0;
	INFO_LOG("server dram total size %ld",server->dram_total_size);
	INFO_LOG("server nvm total size %ld",server->nvm_total_size);
	INIT_LIST_HEAD(&server->area_list);
	INIT_LIST_HEAD(&server->more_area_list);

	server->cur_area = dhmp_area_create(true, SINGLE_AREA_SIZE);
    server->send_area = dhmp_area_create(true, SINGLE_AREA_SIZE);
    server->recv_area = dhmp_area_create(true, SINGLE_AREA_SIZE);

    /* 创建event_channel进行连接建立的准备*/
    err=dhmp_transport_listen(server->listen_trans,
					server->config.net_infos[server->config.curnet_id].port);
	if(err)
		exit(- 1);
	
	/* 准备去连接头尾服务器 */


}