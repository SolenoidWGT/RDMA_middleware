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


/**
 *	the success work completion handler function
 */
static void dhmp_wc_success_handler(struct ibv_wc* wc)
{
	struct dhmp_task *task_ptr;
	struct dhmp_transport *rdma_trans;
	struct dhmp_msg msg;
	
	task_ptr=(struct dhmp_task*)(uintptr_t)wc->wr_id;
	rdma_trans=task_ptr->rdma_trans;

	/*read the msg content from the task_ptr sge addr*/
	msg.msg_type=*(enum dhmp_msg_type*)task_ptr->sge.addr;
	msg.data_size=*(size_t*)(task_ptr->sge.addr+sizeof(enum dhmp_msg_type));
	msg.data=task_ptr->sge.addr+sizeof(enum dhmp_msg_type)+sizeof(size_t);
	
	switch(wc->opcode)
	{
		case IBV_WC_SEND:
			break;
		case IBV_WC_RECV:
			dhmp_wc_recv_handler(rdma_trans, &msg);
			dhmp_post_recv(rdma_trans, task_ptr->sge.addr);
			break;
		case IBV_WC_RDMA_WRITE:
#ifdef DHMP_MR_REUSE_POLICY
			// 如果该区域的内存小于RDMA_SEND_THREASHOLD，则回收（不是释放）该块注册内存，用于下一次的数据传输
			if (task_ptr->sge.length <= RDMA_SEND_THREASHOLD)
			{
				pthread_mutex_lock(&client_mgr->mutex_send_mr_list);
				list_add(&task_ptr->smr->send_mr_entry, &client_mgr->send_mr_list);
				pthread_mutex_unlock(&client_mgr->mutex_send_mr_list);
			}
#endif
			task_ptr->addr_info->write_flag=false;
			task_ptr->done_flag=true;
			break;
		case IBV_WC_RDMA_READ:
			task_ptr->done_flag=true;
			break;
		default:
			ERROR_LOG("unknown opcode:%s",
			            dhmp_wc_opcode_str(wc->opcode));
			break;
	}
}

/**
 *	dhmp_wc_error_handler:handle the error work completion.
 */
static void dhmp_wc_error_handler(struct ibv_wc* wc)
{
	if(wc->status==IBV_WC_WR_FLUSH_ERR)
	{
		// INFO_LOG("work request flush");
	}
	else
		ERROR_LOG("wc status is [%s]",
		            ibv_wc_status_str(wc->status));
}

/**
 *	dhmp_comp_channel_handler:create a completion channel handler
 *  note:set the following function to the cq handle work completion
 *  epoll回调函数入口
 */
void dhmp_comp_channel_handler(int fd, void* data)
{
	struct dhmp_cq* dcq =(struct dhmp_cq*) data;
	struct ibv_cq* cq;
	void* cq_ctx;
	struct ibv_wc wc;
	int err=0;

	err=ibv_get_cq_event(dcq->comp_channel, &cq, &cq_ctx);
	if(err)
	{
		ERROR_LOG("ibv get cq event error.");
		return ;
	}

	ibv_ack_cq_events(dcq->cq, 1);
	err=ibv_req_notify_cq(dcq->cq, 0);
	if(err)
	{
		ERROR_LOG("ibv req notify cq error.");
		return ;
	}

	while(ibv_poll_cq(dcq->cq, 1, &wc))
	{
		if(wc.status==IBV_WC_SUCCESS)
			dhmp_wc_success_handler(&wc);
		else
			dhmp_wc_error_handler(&wc);
	}
}