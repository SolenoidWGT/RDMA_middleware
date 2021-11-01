#include "dhmp.h"
#include "dhmp_log.h"
#include "dhmp_config.h"
#include "dhmp_context.h"
#include "dhmp_dev.h"
#include "dhmp_transport.h"
#include "dhmp_task.h"
#include "dhmp_work.h"
#include "dhmp_client.h"
#include "dhmp_server.h"
#include "dhmp_log.h"
#include "mid_rdma_utils.h"
#include "log_copy.h"


struct dhmp_transport* dhmp_get_trans_from_addr(void *dhmp_addr)
{
	long long node_index=(long long)dhmp_addr;
	node_index=node_index>>48;
	return client_mgr->connect_trans[node_index];
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
	pthread_mutex_lock(&client_mgr->mutex_work_list);
	list_add_tail(&work->work_entry, &client_mgr->work_list);
	pthread_mutex_unlock(&client_mgr->mutex_work_list);
	while(!send_work.done_flag);
	free(work);
	return 0;
}


void *dhmp_malloc(size_t length, int nodeid)
{
	struct dhmp_transport *rdma_trans=NULL;
	struct dhmp_malloc_work malloc_work;
	struct dhmp_work *work;
	struct dhmp_addr_info *addr_info;
	
	if(length<=0)
	{
		ERROR_LOG("length is error.");
		goto out;
	}

	/*select which node to alloc nvm memory*/
	rdma_trans = dhmp_client_node_select_by_id(nodeid);
	if(!rdma_trans)
	{
		ERROR_LOG("don't exist remote server_instance.");
		goto out;
	}

	work=malloc(sizeof(struct dhmp_work));
	if(!work)
	{
		ERROR_LOG("allocate memory error.");
		goto out;
	}
	
	addr_info=malloc(sizeof(struct dhmp_addr_info));
	if(!addr_info)
	{
		ERROR_LOG("allocate memory error.");
		goto out_work;
	}
	addr_info->nvm_mr.length=0;
	addr_info->dram_mr.addr=NULL;
	
	malloc_work.addr_info=addr_info;
	malloc_work.rdma_trans=rdma_trans;
	malloc_work.length=length;
	malloc_work.done_flag=false;

	work->work_type=DHMP_WORK_MALLOC;
	work->work_data=&malloc_work;

	pthread_mutex_lock(&client_mgr->mutex_work_list);
	list_add_tail(&work->work_entry, &client_mgr->work_list);
	pthread_mutex_unlock(&client_mgr->mutex_work_list);
	
	while(!malloc_work.done_flag);

	free(work);
	
	return malloc_work.res_addr;

out_work:
	free(work);
out:
	return NULL;
}


// buff
void dhmp_buff_malloc(int nodeid, void ** buff_mate_addr, void** buff_addr)
{
	struct dhmp_transport *rdma_trans=NULL;
	struct dhmp_buff_malloc_work buff_malloc_work;
	struct dhmp_work *work;
	struct dhmp_addr_info * buff_addr_info;
	struct dhmp_addr_info * buff_mate_addr_info;

	/*select which node to alloc nvm memory*/
	rdma_trans = dhmp_client_node_select_by_id(nodeid);
	if(!rdma_trans)
	{
		ERROR_LOG("don't exist remote server_instance.");
		goto out;
	}

	work = malloc(sizeof(struct dhmp_work));
	if(!work)
	{
		ERROR_LOG("allocate memory error.");
		goto out;
	}
	// addr_info 不要free，其会被插入到hash表中。
	buff_addr_info = malloc(sizeof(struct dhmp_addr_info));
	buff_mate_addr_info = malloc(sizeof(struct dhmp_addr_info));

	if(!buff_addr_info || !buff_mate_addr_info)
	{
		ERROR_LOG("allocate memory error.");
		goto out_work;
	}

	buff_addr_info->nvm_mr.length=0;
	buff_addr_info->dram_mr.addr=NULL;

	buff_mate_addr_info->nvm_mr.length = 0;
	buff_mate_addr_info->dram_mr.addr = NULL;

	buff_malloc_work.node_id = server_instance->server_id;
	buff_malloc_work.buff_addr_info=buff_addr_info;
	buff_malloc_work.buff_mate_addr_info=buff_mate_addr_info;
	buff_malloc_work.rdma_trans=rdma_trans;
	buff_malloc_work.done_flag=false;
	buff_malloc_work.done_flag_recv = false;

	work->work_type = DHMP_BUFF_MALLOC;
	work->work_data = &buff_malloc_work;

	pthread_mutex_lock(&client_mgr->mutex_work_list);
	list_add_tail(&work->work_entry, &client_mgr->work_list);
	pthread_mutex_unlock(&client_mgr->mutex_work_list);

	while(!buff_malloc_work.done_flag);

	free(work);
	*buff_mate_addr = buff_malloc_work.buff_mate_addr;
	*buff_addr = buff_malloc_work.buff_addr;

	//return malloc_work.res_addr;
out:
	return;

out_work:
	free(work);
}

// WGT
int dhmp_read(void *dhmp_addr, void * local_buf, size_t count, 
					off_t offset, bool is_atomic)
{
	struct dhmp_transport *rdma_trans=NULL;
	struct dhmp_rw_work rwork;
	struct dhmp_work *work;
	
	rdma_trans=dhmp_get_trans_from_addr(dhmp_addr);;
	if(!rdma_trans||rdma_trans->trans_state!=DHMP_TRANSPORT_STATE_CONNECTED)
	{
		ERROR_LOG("rdma connection error.");
		return -1;
	}

	work=malloc(sizeof(struct dhmp_work));
	if(!work)
	{
		ERROR_LOG("allocate memory error.");
		return -1;
	}
	
	rwork.done_flag=false;
	rwork.length=count;
	rwork.local_addr=local_buf;
	rwork.dhmp_addr=dhmp_addr;	
	rwork.rdma_trans=rdma_trans;
	rwork.offset = offset; // wgt
	rwork.is_atomic = is_atomic;// wgt
	
	work->work_type=DHMP_WORK_READ;
	work->work_data=&rwork;
	
	pthread_mutex_lock(&client_mgr->mutex_work_list);
	list_add_tail(&work->work_entry, &client_mgr->work_list);
	pthread_mutex_unlock(&client_mgr->mutex_work_list);

	while(!rwork.done_flag);

	free(work);
	
	return 0;
}

// WGT
int dhmp_write(void *dhmp_addr, void * local_buf, size_t count, 
						off_t offset, bool is_atomic)		
{
	struct dhmp_transport *rdma_trans=NULL;
	struct dhmp_rw_work wwork;
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
	wwork.done_flag=false;
	wwork.length=count;
	wwork.local_addr=local_buf;
	wwork.dhmp_addr= dhmp_addr; 
	wwork.rdma_trans=rdma_trans;
	wwork.offset = offset;		// WGT
	wwork.is_atomic = is_atomic;// wgt
			
	work->work_type=DHMP_WORK_WRITE;
	work->work_data=&wwork;
	
	pthread_mutex_lock(&client_mgr->mutex_work_list);
	list_add_tail(&work->work_entry, &client_mgr->work_list);
	pthread_mutex_unlock(&client_mgr->mutex_work_list);
	
	while(!wwork.done_flag);

	free(work);
	
	return 0;
}

/* 
 * WGT: add send ack api for top app 
 * In low level, send ack use two-side rdma primitive.
 * 
 * Arg: 
 * 		nodeid : which node you want to send ack.
 */
enum response_state
dhmp_ack(int nodeid, enum request_state acktype, bool isClient)
{
	struct dhmp_transport *rdma_trans=NULL;
	struct dhmp_ack_work ackwork;
	struct dhmp_work *work;
	enum response_state re;

	/*select which node to alloc nvm memory*/
	if (isClient)
		rdma_trans = dhmp_client_node_select_by_id(nodeid);
	else
		rdma_trans = dhmp_server_node_select_by_id(nodeid);

	if(!rdma_trans)
	{
		ERROR_LOG("don't exist remote server_instance.");
		return -1;
	}

	if(!rdma_trans||rdma_trans->trans_state!=DHMP_TRANSPORT_STATE_CONNECTED)
	{
		ERROR_LOG("rdma connection error.");
		return -1;
	}

	work=malloc(sizeof(struct dhmp_work));
	if(!work)
	{
		ERROR_LOG("allocate memory error.");
		return -1;
	}
	
	ackwork.rdma_trans = rdma_trans;
	ackwork.done_flag = false;
	ackwork.done_flag_recv = false;
	ackwork.ack_flag = acktype;

	if (acktype == RQ_FINISHED_LOOP)
	{
		logEntry * log;
		if(!emptyQueue(executing_queue))
			topQueue(executing_queue, &log);
		else
		{
			ERROR_LOG("ACK Null log!");
			return -1;
		}
		ackwork.log_ptr = log->mateData.log_ptr;
	}
	else if(acktype == RQ_PORT_NUMBER)
	{
		ackwork.log_ptr = (void*) server_instance->server_id;
	}

	work->work_type=DHMP_WORK_ACK;
	work->work_data=&ackwork;
	
	pthread_mutex_lock(&client_mgr->mutex_work_list);
	list_add_tail(&work->work_entry, &client_mgr->work_list);
	pthread_mutex_unlock(&client_mgr->mutex_work_list);

	while(!ackwork.done_flag);

	re = ackwork.res_ack_flag;

	free(work);
	
	return re;
}

