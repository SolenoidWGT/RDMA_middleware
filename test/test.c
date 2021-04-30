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
#define COUNT 100000
#define SIZE 1024*8

//unsigned long get_mr_time = 0;




int main(int argc,char *argv[])
{
	struct dhmp_server * mid_server;

	/*init list about rdma device*/
	mid_server = dhmp_server_init();
    INFO_LOG("server->server_id is %d", mid_server->server_id);
	dhmp_client_init(SIZE*2, mid_server->server_id);
	/* wait connect establish*/
	sleep(5);
	char * base = (char * )malloc(SIZE * sizeof(char));
	void* remote_addr = dhmp_malloc(SIZE, client_find_server_id());
	dhmp_send(remote_addr, base, SIZE, true);
	dhmp_send(remote_addr, base, SIZE, false);
	dhmp_server_destroy();
	return 0;
}
