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
#define COUNT 100000
#define SIZE 1024*8

//unsigned long get_mr_time = 0;




int main(int argc,char *argv[])
{
	struct dhmp_server * mid_server;

	/*init list about rdma device*/
	mid_server = dhmp_server_init();
    INFO_LOG("server->server_id is %d", mid_server->server_id);
	dhmp_client_init(1024, mid_server->server_id);
	return 0;
}
