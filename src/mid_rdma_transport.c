#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <arpa/inet.h>

#include "dhmp.h"
#include "dhmp_transport.h"
#include "../include/dhmp_work.h"
#include "../include/dhmp_task.h"
#include "../include/dhmp_client.h"
#include "../include/dhmp_log.h"
#include "dhmp_dev.h"


void dhmp_post_recv(struct dhmp_transport* rdma_trans, void *addr);
void dhmp_post_all_recv(struct dhmp_transport *rdma_trans);





/*
 *	two sided RDMA operations
 */
void dhmp_post_recv(struct dhmp_transport* rdma_trans, void *addr)
{
	struct ibv_recv_wr recv_wr, *bad_wr_ptr=NULL;
	struct ibv_sge sge;
	struct dhmp_task *recv_task_ptr;
	int err=0;

	if(rdma_trans->trans_state>DHMP_TRANSPORT_STATE_CONNECTED)
		return ;
	
	recv_task_ptr=dhmp_recv_task_create(rdma_trans, addr);
	if(!recv_task_ptr)
	{
		ERROR_LOG("create recv task error.");
		return ;
	}
	
	recv_wr.wr_id=(uintptr_t)recv_task_ptr;
	recv_wr.next=NULL;
	recv_wr.sg_list=&sge;
	recv_wr.num_sge=1;

	sge.addr=(uintptr_t)recv_task_ptr->sge.addr;
	sge.length=recv_task_ptr->sge.length;
	sge.lkey=recv_task_ptr->sge.lkey;
	
	err=ibv_post_recv(rdma_trans->qp, &recv_wr, &bad_wr_ptr);
	if(err)
		ERROR_LOG("ibv post recv error.");
	
}

/**
 *	dhmp_post_all_recv:loop call the dhmp_post_recv function
 */
void dhmp_post_all_recv(struct dhmp_transport *rdma_trans)
{
	int i, single_region_size=0;

	if(rdma_trans->is_poll_qp)
		single_region_size=SINGLE_POLL_RECV_REGION;
	else
		single_region_size=SINGLE_NORM_RECV_REGION;
	
	for(i=0; i<RECV_REGION_SIZE/single_region_size; i++)
	{
		dhmp_post_recv(rdma_trans, 
			rdma_trans->recv_mr.addr+i*single_region_size);
	}
}



void dhmp_post_send(struct dhmp_transport* rdma_trans, struct dhmp_msg* msg_ptr)
{
	struct ibv_send_wr send_wr,*bad_wr=NULL;
	struct ibv_sge sge;
	struct dhmp_task *send_task_ptr;
	int err=0;
	
	if(rdma_trans->trans_state!=DHMP_TRANSPORT_STATE_CONNECTED)
		return ;
	send_task_ptr=dhmp_send_task_create(rdma_trans, msg_ptr);
	if(!send_task_ptr)
	{
		ERROR_LOG("create recv task error.");
		return ;
	}
	
	memset ( &send_wr, 0, sizeof ( send_wr ) );
	send_wr.wr_id= ( uintptr_t ) send_task_ptr;
	send_wr.sg_list=&sge;
	send_wr.num_sge=1;
	send_wr.opcode=IBV_WR_SEND;
	send_wr.send_flags=IBV_SEND_SIGNALED;

	sge.addr= ( uintptr_t ) send_task_ptr->sge.addr;
	sge.length=send_task_ptr->sge.length;
	sge.lkey=send_task_ptr->sge.lkey;

	err=ibv_post_send ( rdma_trans->qp, &send_wr, &bad_wr );
	if ( err )
		ERROR_LOG ( "ibv_post_send error." );
}


/*
one side rdma
*/

static struct dhmp_send_mr* dhmp_get_mr_from_send_list(struct dhmp_transport* rdma_trans, void* addr, int length )
{
	struct dhmp_send_mr *res,*tmp;
	void* new_addr=NULL;

	res=(struct dhmp_send_mr* )malloc(sizeof(struct dhmp_send_mr));
	if(!res)
	{
		ERROR_LOG("allocate memory error.");
		return NULL;
	}
	
	res->mr=ibv_reg_mr(rdma_trans->device->pd,
						addr, length, IBV_ACCESS_LOCAL_WRITE);
	if(!res->mr)
	{
		ERROR_LOG("ibv register memory error.");
		goto error;
	}
		
	return res;

error:
	free ( res );
	return NULL;
}


int dhmp_rdma_read_after_write ( struct dhmp_transport* rdma_trans, struct dhmp_addr_info *addr_info, struct ibv_mr* mr, void* local_addr, int length)
{
	struct timespec start_time, end_time;

	struct dhmp_task* write_task;
	struct ibv_send_wr send_wr,*bad_wr=NULL;
	struct ibv_sge sge;
	struct dhmp_send_mr* smr=NULL;
	int err=0;
	
	//clock_gettime(CLOCK_MONOTONIC, &start_time);
	smr=dhmp_get_mr_from_send_list(rdma_trans, local_addr, length);
	// clock_gettime(CLOCK_MONOTONIC, &end_time);
	// get_mr_time += ((end_time.tv_sec * 1000000000) + end_time.tv_nsec) -
    //                     ((start_time.tv_sec * 1000000000) + start_time.tv_nsec);
						
	write_task=dhmp_write_task_create(rdma_trans, smr, length);
	if(!write_task)
	{
		ERROR_LOG("allocate memory error.");
		return -1;
	}
	write_task->addr_info=addr_info;
	
	memset(&send_wr, 0, sizeof(struct ibv_send_wr));

	send_wr.wr_id= ( uintptr_t ) write_task;
	send_wr.opcode=IBV_WR_RDMA_WRITE;
	send_wr.sg_list=&sge;
	send_wr.num_sge=1;
	send_wr.send_flags=IBV_SEND_SIGNALED;
	send_wr.wr.rdma.remote_addr= ( uintptr_t ) mr->addr;
	send_wr.wr.rdma.rkey=mr->rkey;

	sge.addr= ( uintptr_t ) write_task->sge.addr;
	sge.length=write_task->sge.length;
	sge.lkey=write_task->sge.lkey;

	struct dhmp_task *read_task;
	struct ibv_send_wr send_wr2, *bad_wr2 = NULL;
	struct ibv_sge sge2;
	read_task = dhmp_read_task_create(rdma_trans, client->read_mr[rdma_trans->node_id], 1);
	if (!read_task)
	{
		ERROR_LOG("allocate memory error.");
		return -1;
	}

	memset(&send_wr2, 0, sizeof(struct ibv_send_wr));

	send_wr2.wr_id = (uintptr_t)read_task;
	send_wr2.opcode = IBV_WR_RDMA_READ;
	send_wr2.sg_list = &sge2;
	send_wr2.num_sge = 1; // or 1
	send_wr2.send_flags = IBV_SEND_SIGNALED;
	send_wr2.wr.rdma.remote_addr = (uintptr_t)mr->addr + length - 1;
	send_wr2.wr.rdma.rkey = mr->rkey;

	sge2.addr = (uintptr_t)read_task->sge.addr;
	sge2.length = read_task->sge.length;
	sge2.lkey = read_task->sge.lkey;

	err=ibv_post_send ( rdma_trans->qp, &send_wr, &bad_wr );
	if ( err )
	{
		ERROR_LOG("ibv_post_send error");
		exit(-1);
		goto error;
	}

	DEBUG_LOG("before read_mr[%d] addr content is %s", rdma_trans->node_id, client->read_mr[rdma_trans->node_id]->mr->addr);
	err=ibv_post_send ( rdma_trans->qp, &send_wr2, &bad_wr2 );
	if ( err )
	{
		ERROR_LOG("ibv_post_send error");
		exit(-1);
		goto error;
	}

	while (!write_task->done_flag);
	while (!read_task->done_flag);
	DEBUG_LOG("after read_mr[%d] addr content is %s", rdma_trans->node_id, client->read_mr[rdma_trans->node_id]->mr->addr);

	return 0;
	
error:
	return -1;
}

int dhmp_rdma_read(struct dhmp_transport* rdma_trans, struct ibv_mr* mr, void* local_addr, int length)
{
	struct dhmp_task* read_task;
	struct ibv_send_wr send_wr,*bad_wr=NULL;
	struct ibv_sge sge;
	struct dhmp_send_mr* smr=NULL;
	int err=0;
	
	smr=dhmp_get_mr_from_send_list(rdma_trans, local_addr, length);
	read_task=dhmp_read_task_create(rdma_trans, smr, length);
	if ( !read_task )
	{
		ERROR_LOG ( "allocate memory error." );
		return -1;
	}

	memset(&send_wr, 0, sizeof(struct ibv_send_wr));

	send_wr.wr_id= ( uintptr_t ) read_task;
	send_wr.opcode=IBV_WR_RDMA_READ;
	send_wr.sg_list=&sge;
	send_wr.num_sge=1;
	send_wr.send_flags=IBV_SEND_SIGNALED;
	send_wr.wr.rdma.remote_addr=(uintptr_t)mr->addr;
	send_wr.wr.rdma.rkey=mr->rkey;

	sge.addr=(uintptr_t)read_task->sge.addr;
	sge.length=read_task->sge.length;
	sge.lkey=read_task->sge.lkey;
	
	err=ibv_post_send(rdma_trans->qp, &send_wr, &bad_wr);
	if(err)
	{
		ERROR_LOG("ibv_post_send error");
		goto error;
	}

	DEBUG_LOG("before local addr is %s", local_addr);
	
	while(!read_task->done_flag);
	

	ibv_dereg_mr(smr->mr);
	free(smr);
		
	DEBUG_LOG("local addr content is %s", local_addr);

	return 0;
error:
	return -1;
}

int dhmp_rdma_write(struct dhmp_transport* rdma_trans, struct dhmp_addr_info* addr_info,
	struct ibv_mr* mr, void* local_addr, int length)
{
	struct dhmp_task* write_task;
	struct ibv_send_wr send_wr, * bad_wr = NULL;
	struct ibv_sge sge;
	struct dhmp_send_mr* smr = NULL;
	int err = 0;

	// 由dhmp_get_mr_from_send_list函数解决内存区域复用的问题
	smr = dhmp_get_mr_from_send_list(rdma_trans, local_addr, length);
	write_task = dhmp_write_task_create(rdma_trans, smr, length);
	if (!write_task)
	{
		ERROR_LOG("allocate memory error.");
		return -1;
	}
	write_task->addr_info = addr_info;

	memset(&send_wr, 0, sizeof(struct ibv_send_wr));

	send_wr.wr_id = (uintptr_t)write_task;
	send_wr.opcode = IBV_WR_RDMA_WRITE;
	send_wr.sg_list = &sge;
	send_wr.num_sge = 1;
	send_wr.send_flags = IBV_SEND_SIGNALED;
	send_wr.wr.rdma.remote_addr = (uintptr_t)mr->addr;
	send_wr.wr.rdma.rkey = mr->rkey;

	sge.addr = (uintptr_t)write_task->sge.addr;
	sge.length = write_task->sge.length;
	sge.lkey = write_task->sge.lkey;
	err = ibv_post_send(rdma_trans->qp, &send_wr, &bad_wr);
	if (err)
	{
		ERROR_LOG("ibv_post_send error");
		exit(-1);
		goto error;
	}

	while (!write_task->done_flag);
	ibv_dereg_mr(smr->mr);
	free(smr);
	return 0;

error:
	return -1;
}

/**
 *	return the work completion operation code string.
 */
const char* dhmp_wc_opcode_str(enum ibv_wc_opcode opcode)
{
	switch(opcode)
	{
		case IBV_WC_SEND:
			return "IBV_WC_SEND";
		case IBV_WC_RDMA_WRITE:
			return "IBV_WC_RDMA_WRITE";
		case IBV_WC_RDMA_READ:
			return "IBV_WC_RDMA_READ";
		case IBV_WC_COMP_SWAP:
			return "IBV_WC_COMP_SWAP";
		case IBV_WC_FETCH_ADD:
			return "IBV_WC_FETCH_ADD";
		case IBV_WC_BIND_MW:
			return "IBV_WC_BIND_MW";
		case IBV_WC_RECV:
			return "IBV_WC_RECV";
		case IBV_WC_RECV_RDMA_WITH_IMM:
			return "IBV_WC_RECV_RDMA_WITH_IMM";
		default:
			return "IBV_WC_UNKNOWN";
	};
}

