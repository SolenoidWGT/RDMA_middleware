#include "dhmp.h"
#include "dhmp_log.h"
#include "dhmp_config.h"
#include "dhmp_context.h"
#include "dhmp_dev.h"
#include "dhmp_transport.h"
#include "dhmp_task.h"
#include "dhmp_work.h"
#include "dhmp_client.h"
#include "../include/dhmp_log.h"

extern struct dhmp_client *client;


struct dhmp_transport* dhmp_get_trans_from_addr(void *dhmp_addr)
{
	long long node_index=(long long)dhmp_addr;
	node_index=node_index>>48;
	return client->connect_trans[node_index];
}

int dhmp_send(void *dhmp_addr, void * local_buf, size_t length, bool is_write)
{
	struct dhmp_transport *rdma_trans=NULL;
	struct dhmp_send_work send_work;
	struct dhmp_work *work;

	rdma_trans=dhmp_get_trans_from_addr(dhmp_addr);
	if(!rdma_trans||rdma_trans->trans_state!=DHMP_TRANSPORT_STATE_CONNECTED)
	{
		ERROR_LOG("rdma connection error.");
		return -1;
	}

	work=malloc(sizeof(struct dhmp_work));
	if(!work)
	{
		ERROR_LOG("alloc memory error.");
		return -1;
	}
	send_work.done_flag=false;
	send_work.recv_flag=false;
	send_work.length=length;
	send_work.local_addr=local_buf;
	send_work.dhmp_addr=dhmp_addr;
	send_work.rdma_trans=rdma_trans;
	send_work.is_write = is_write;
			
	work->work_type=DHMP_WORK_SEND;
	work->work_data=&send_work;
	pthread_mutex_lock(&client->mutex_work_list);
	list_add_tail(&work->work_entry, &client->work_list);
	pthread_mutex_unlock(&client->mutex_work_list);
	while(!send_work.done_flag);
	free(work);
	return 0;
}



