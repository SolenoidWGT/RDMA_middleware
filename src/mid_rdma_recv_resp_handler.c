#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <arpa/inet.h>

#include "dhmp.h"
#include "dhmp_transport.h"
#include "dhmp_work.h"
#include "dhmp_task.h"
#include "dhmp_client.h"
#include "dhmp_log.h"
#include "dhmp_dev.h"
#include "dhmp_server.h"
#include "mid_rdma_utils.h"

#include "log_copy.h"


static void dhmp_malloc_request_handler(struct dhmp_transport* rdma_trans,
												struct dhmp_msg* msg)
{
	struct dhmp_mc_response response;
	struct dhmp_msg res_msg;
	bool res=true;
	struct dhmp_device * dev;
	struct ibv_mr * mr = NULL;
	size_t re_length = 0;
	char * addr;


	memcpy ( &response.req_info, msg->data, sizeof(struct dhmp_mc_request));
	INFO_LOG ( "client req size %d",  response.req_info.req_size);
	re_length = response.req_info.req_size;


	dev=dhmp_get_dev_from_server();
	mr = dhmp_memory_malloc_register(dev->pd, re_length, 2);
	if(!mr){
		ERROR_LOG("dhmp_malloc_request_handler Fail!");
		goto req_error;
	}

	memcpy(&response.mr, mr, sizeof(struct ibv_mr));
	DEBUG_LOG("malloc addr %p lkey %ld length is %d",
			mr->addr, mr->lkey, mr->length );
	

	res_msg.msg_type = DHMP_MSG_MALLOC_RESPONSE;
	res_msg.data_size = sizeof(struct dhmp_mc_response);
	res_msg.data= &response;
	dhmp_post_send(rdma_trans, &res_msg);
	/*count the server_instance memory,and inform the watcher*/
	rdma_trans->nvm_used_size+=response.mr.length;

	return ;

req_error:
	/*transmit a message of DHMP_MSG_MALLOC_ERROR*/
	res_msg.msg_type=DHMP_MSG_MALLOC_ERROR;
	res_msg.data_size=sizeof(struct dhmp_mc_response);
	res_msg.data=&response;

	dhmp_post_send ( rdma_trans, &res_msg );

	return ;
}

static void dhmp_malloc_response_handler(struct dhmp_transport* rdma_trans,
													struct dhmp_msg* msg)

{
	struct dhmp_mc_response response_msg;
	struct dhmp_addr_info *addr_info;

	memcpy(&response_msg, msg->data, sizeof(struct dhmp_mc_response));
	
	addr_info = response_msg.req_info.addr_info;
	addr_info->read_cnt=addr_info->write_cnt = 0;
	memcpy(&addr_info->nvm_mr, &response_msg.mr, sizeof(struct ibv_mr));

	DEBUG_LOG("response mr addr %p lkey %ld",
			addr_info->nvm_mr.addr, addr_info->nvm_mr.lkey);

}


static void dhmp_malloc_buff_request_handler(struct dhmp_transport* rdma_trans,
												struct dhmp_msg* msg)

{
	int re;
	struct dhmp_buff_response response;
	struct dhmp_msg res_msg;
	bool res=true;
	struct dhmp_device * dev;
	struct ibv_mr * mr = NULL;
	size_t re_length = 0;
	char * addr;

	memcpy ( &response.req_info, msg->data, sizeof(struct dhmp_buff_request));
	INFO_LOG ( "Recv buff request from node [%d]",  response.req_info.node_id);
	response.node_id = server_instance->server_id;

	dev=dhmp_get_dev_from_server();

	/* 
	 * If in first time, we init buffer, next time will return 
	 * metadata information directly .
	 */
	if(local_recv_buff == NULL ||  
	   local_recv_buff_mate == NULL)
	{
		LocalRingbuff     * tmp_buff_ptr;
		LocalMateRingbuff * tmp_buff_mate_ptr;
		
		/* Double checked lock */
		pthread_mutex_lock(&buff_init_lock);
		if(local_recv_buff == NULL ||
		   local_recv_buff_mate == NULL)
		{
			tmp_buff_mate_ptr = (LocalMateRingbuff*) malloc(sizeof(LocalMateRingbuff));
			/* 
			 * dhmp_memory_register will also allocate memory and 
			 * register memory.
			 */
			re = dhmp_memory_register(dev->pd, &tmp_buff_mate_ptr->buff_mate_mr, \
										sizeof(LocalRingbuff));

			if(re !=0)
			{
				ERROR_LOG("BUFF: malloc and register Local_Recv_Buff_MR Fail!");
				free(tmp_buff_mate_ptr);
				goto req_error;
			}

			tmp_buff_ptr = (LocalRingbuff*) tmp_buff_mate_ptr->buff_mate_mr.addr;
			tmp_buff_ptr->wr_pointer = 0;
			tmp_buff_ptr->rd_pointer = 0;
			tmp_buff_ptr->size = TOTAL_SIZE;
			tmp_buff_ptr->rd_key_pointer = 0;

			re =  dhmp_memory_register(dev->pd, &tmp_buff_ptr->buff_mr, TOTAL_SIZE);
			if(re !=0)
			{
				ERROR_LOG("BUFF: malloc and register buff Fail!");
				ibv_dereg_mr(tmp_buff_mate_ptr->buff_mate_mr.mr);
				free(tmp_buff_mate_ptr->buff_mate_mr.addr);
				free(tmp_buff_mate_ptr);
				goto req_error;
			}
			tmp_buff_ptr->buff_addr = tmp_buff_ptr->buff_mr.addr;

			/* Init buffer with zero, because we need use tag to indicate finishment */
			memset(tmp_buff_ptr->buff_addr, 0, tmp_buff_ptr->size);	

			/* At the very last moment can we assign value to global variables */
			local_recv_buff = tmp_buff_ptr;
			local_recv_buff_mate = tmp_buff_mate_ptr;

			INFO_LOG("Local mate addr is %p, buff addr is %p", local_recv_buff_mate, local_recv_buff->buff_addr);
		}
		pthread_mutex_unlock(&buff_init_lock);
	}

	/* It is safe for all node to copy mate data */
	memcpy(&response.mr_buff, local_recv_buff_mate->buff_mate_mr.mr, sizeof(struct ibv_mr));
	memcpy(&response.mr_data, local_recv_buff->buff_mr.mr, sizeof(struct ibv_mr));

	DEBUG_LOG("BUFF: malloc Buffer addr sucess");

	res_msg.msg_type = DHMP_BUFF_MALLOC_RESPONSE;
	res_msg.data_size = sizeof(struct dhmp_buff_response);
	res_msg.data= &response;
	dhmp_post_send(rdma_trans, &res_msg);

	return ;
req_error:
	/*transmit a message of DHMP_MSG_MALLOC_ERROR*/
	res_msg.msg_type=DHMP_BUFF_MALLOC_ERROR;
	res_msg.data_size=sizeof(struct dhmp_buff_response);
	res_msg.data=&response;
	dhmp_post_send ( rdma_trans, &res_msg );

	return ;
}


// static void dhmp_ack_request_handler(struct dhmp_transport* rdma_trans,
// 												struct dhmp_msg* msg)
// {
// 	/* Get ack flag type from struct dhmp_msg */
// 	struct dhmp_ack_request ack;
// 	memcpy(&ack, msg->data, sizeof(struct dhmp_ack_request));

// 	switch (ack.ack_flag)
// 	{
// 		case middware_INIT:
// 			INFO_LOG ( "Get ack:[middware_INIT] from node [%d]",  ack.node_id);
// 			break;
// 		case middware_WAIT_MAIN_NODE:
// 			INFO_LOG ( "Get ack:[middware_WAIT_MAIN_NODE], from node [%d]",  ack.node_id);
// 			break;
// 		case middware_WAIT_SUB_NODE:
// 			INFO_LOG ( "Get ack:[middware_WAIT_SUB_NODE], from node [%d]",  ack.node_id);
// 			break;
// 		case middware_WAIT_MATE_DATA:
// 			INFO_LOG ( "Get ack:[middware_WAIT_MATE_DATA], from node [%d]",  ack.node_id);
// 			break;
// 		default:
// 			INFO_LOG ( "Unkown middware state from node [%d]",  ack.node_id);
// 			break;
// 	}

// 	pthread_mutex_lock(&server_instance->mutex_client_list);
// 	list_for_each_entry(rdma_trans, &server_instance->client_list, client_entry)
// 	{
// 		if (rdma_trans->node_id == ack.node_id)
// 		{
// 			rdma_trans->trans_mid_state = ack.ack_flag;
// 			break;
// 		}
// 	}
// 	pthread_mutex_unlock(&server_instance->mutex_client_list);

// 	return ;
// }

static void dhmp_ack_request_handler(struct dhmp_transport* rdma_trans,
												struct dhmp_msg* msg)
{
	/* Get ack flag type from struct dhmp_msg */
	struct dhmp_ack_request ack;
	struct dhmp_ack_response response;
	struct dhmp_msg res_msg;
	struct dhmp_transport* expect_rdma_trans;
	int peer_server_id;
	int i;

	memcpy(&ack, msg->data, sizeof(struct dhmp_ack_request));
	response.node_id = server_instance->server_id;
	memcpy(&response.req_info, &ack, sizeof(struct dhmp_ack_request));

	switch (ack.ack_flag)
	{
		case RQ_BUFFER_STATE:
			INFO_LOG ( "Get ack request :[RQ_BUFFER_STATE] from node [%d]",  ack.node_id);
			pthread_mutex_lock(&buff_init_lock);
			if(local_recv_buff == NULL || local_recv_buff_mate == NULL)
				response.res_ack_flag = RS_BUFFER_NOREADY;
			else
				response.res_ack_flag = RS_BUFFER_READY;
			pthread_mutex_unlock(&buff_init_lock);
			break;

		/* hyperloop 实现 */
		case RQ_FINISHED_LOOP:
			INFO_LOG ( "Get ack request :[RQ_FINISHED_LOOP] from node [%d], ack log_ptr is [%p]",  ack.node_id, ack.log_ptr);
	
			if (ack.log_ptr == NULL)
			{
				ERROR_LOG ("FATEL: log ptr is NULL!");
				exit(0);
			}
			
			((logEntry *)(ack.log_ptr))->arrived_node_tail = true;
			response.res_ack_flag = HYPERLOOP_ONE_LOOP;
			break;
		case RQ_PORT_NUMBER:
			peer_server_id = (int) ack.log_ptr;
	
			INFO_LOG ( "Get ack request :[RQ_PORT_NUMBER] from node [%d], peer node_id is [%d]",  ack.node_id, peer_server_id);

			pthread_mutex_lock(&server_instance->mutex_client_list);
			list_for_each_entry(expect_rdma_trans, &server_instance->client_list, client_entry)
			{
				if (expect_rdma_trans == rdma_trans)
				{
					INFO_LOG("Catch trans peer node id is [%d], true id is [%d]", expect_rdma_trans->node_id, peer_server_id);
					expect_rdma_trans->node_id = peer_server_id;
				}
			}
			pthread_mutex_unlock(&server_instance->mutex_client_list);

			response.res_ack_flag = RS_PORT_NUMBER;
			break;
		default:
			ERROR_LOG ( "Get ack unkonwn response from node [%d]",  ack.node_id);
			break;
	}

	res_msg.msg_type = DHMP_ACK_RESPONSE;
	res_msg.data_size = sizeof(struct dhmp_ack_response);
	res_msg.data= &response;
	dhmp_post_send(rdma_trans, &res_msg);
	return ;
}

static void dhmp_malloc_buff_response_handler(struct dhmp_transport* rdma_trans,
													struct dhmp_msg* msg)

{
	struct dhmp_buff_response response_msg;
	struct dhmp_addr_info * buff_addr_info;
	struct dhmp_addr_info * buff_mate_addr_info;
	memcpy(&response_msg, msg->data, sizeof(struct dhmp_buff_response));
	int peer_id = response_msg.node_id;

	buff_addr_info = response_msg.req_info.buff_addr_info;
	buff_mate_addr_info = response_msg.req_info.buff_mate_addr_info;

	buff_addr_info->read_cnt = 0;
	buff_addr_info->write_cnt = 0;
	buff_mate_addr_info->read_cnt = 0;
	buff_mate_addr_info->write_cnt = 0;

	memcpy(&buff_addr_info->nvm_mr, 	 &response_msg.mr_data, sizeof(struct ibv_mr));
	memcpy(&buff_mate_addr_info->nvm_mr, &response_msg.mr_buff, sizeof(struct ibv_mr));

	DEBUG_LOG("response buff mr addr %p", 			buff_addr_info->nvm_mr.addr);
	DEBUG_LOG("response buff matedata mr addr %p",  buff_mate_addr_info->nvm_mr.addr);
	response_msg.req_info.work->done_flag_recv = true;
}

static void dhmp_ack_response_handler(struct dhmp_transport* rdma_trans,
													struct dhmp_msg* msg)

{
	struct dhmp_ack_response response_msg;
	memcpy(&response_msg, msg->data, sizeof(struct dhmp_ack_response));
	
	switch (response_msg.res_ack_flag)
	{
		case RS_INIT_READY:
			INFO_LOG ( "Get ack response :[RS_INIT_READY] from node [%d]",  response_msg.node_id);
			break;
		case RS_INIT_NOREADY:
			INFO_LOG ( "Get ack response :[RS_INIT_NOREADY] from node [%d]",  response_msg.node_id);
			break;
		case RS_BUFFER_READY:
			INFO_LOG ( "Get ack response :[RS_BUFFER_READY] from node [%d]",  response_msg.node_id);
			break;
		case RS_BUFFER_NOREADY:
			INFO_LOG ( "Get ack response :[RS_BUFFER_NOREADY] from node [%d]",  response_msg.node_id);
			break;
		case HYPERLOOP_ONE_LOOP:
			INFO_LOG ( "Get ack response :[HYPERLOOP_ONE_LOOP] from node [%d]",  response_msg.node_id);
			break;
		case RS_PORT_NUMBER:
			INFO_LOG ( "Get ack response :[RS_PORT_NUMBER] from node [%d]",  response_msg.node_id);
			break;
		default:
			ERROR_LOG ( "Unkown middware response ack from node [%d]", response_msg.node_id);
			break;
	}

	response_msg.req_info.work->res_ack_flag = response_msg.res_ack_flag;
	response_msg.req_info.work->done_flag_recv = true;
}

static void dhmp_malloc_error_handler(struct dhmp_transport* rdma_trans, struct dhmp_msg* msg)
{

}


static void dhmp_free_request_handler(struct dhmp_transport* rdma_trans, struct dhmp_msg* msg_ptr)
{

}

static void dhmp_free_response_handler(struct dhmp_transport* rdma_trans, struct dhmp_msg* msg)
{

}

static void dhmp_send_request_handler(struct dhmp_transport* rdma_trans, struct dhmp_msg* msg)
{
	struct dhmp_send_response response;
	struct dhmp_msg res_msg;
	void * server_addr = NULL;
	void * temp;

	memcpy ( &response.req_info, msg->data, sizeof(struct dhmp_send_request));
	size_t length = response.req_info.req_size;
	INFO_LOG ( "client operate size %d",length);

	/*get server_instance addr from dhmp_addr
		server_addr 来自client向server请求的地址
	*/
	server_addr = response.req_info.server_addr;

	res_msg.msg_type=DHMP_MSG_SEND_RESPONSE;
	
	if(response.req_info.is_write == true) 
	{
		res_msg.data_size=sizeof(struct dhmp_send_response);
		res_msg.data=&response;
		/*may be need mutex*/
		memcpy(server_addr, (msg->data+sizeof(struct dhmp_send_request)),length);
	}
	else
	{
		res_msg.data_size = sizeof(struct dhmp_send_response) + length;
		temp = malloc(res_msg.data_size );
		memcpy(temp,&response,sizeof(struct dhmp_send_response));
		memcpy(temp+sizeof(struct dhmp_send_response),server_addr, length);
		res_msg.data = temp;
	}

	dhmp_post_send(rdma_trans, &res_msg);
}

static void dhmp_send_response_handler(struct dhmp_transport* rdma_trans, struct dhmp_msg* msg)
{
	struct dhmp_send_response response_msg;

	memcpy(&response_msg, msg->data, sizeof(struct dhmp_send_response));


	if (!response_msg.req_info.is_write)
	{
		memcpy(response_msg.req_info.local_addr, 
				msg->data + sizeof(struct dhmp_send_response), response_msg.req_info.req_size);
	}

	struct dhmp_send_work * task = response_msg.req_info.task;
	task->recv_flag = true;
}

/**
 *	dhmp_wc_recv_handler:handle the IBV_WC_RECV event
 */
void dhmp_wc_recv_handler(struct dhmp_transport* rdma_trans,
										struct dhmp_msg* msg)
{
	switch(msg->msg_type)
	{
		case DHMP_MSG_MALLOC_REQUEST:
			dhmp_malloc_request_handler(rdma_trans, msg);
			break;
		case DHMP_MSG_MALLOC_RESPONSE:
			dhmp_malloc_response_handler(rdma_trans, msg);
			break;
		case DHMP_MSG_MALLOC_ERROR:
			dhmp_malloc_error_handler(rdma_trans, msg);
			break;
		case DHMP_MSG_FREE_REQUEST:
			dhmp_free_request_handler(rdma_trans, msg);
			break;
		case DHMP_MSG_FREE_RESPONSE:
			dhmp_free_response_handler(rdma_trans, msg);
			break;
		case DHMP_MSG_SEND_REQUEST:
			dhmp_send_request_handler(rdma_trans, msg);
			break;
		case DHMP_MSG_SEND_RESPONSE:
			dhmp_send_response_handler(rdma_trans, msg);
			break;

		/* WGT:add new request handler */
		case DHMP_BUFF_MALLOC_REQUEST:
			dhmp_malloc_buff_request_handler(rdma_trans, msg);
			break;
		case DHMP_BUFF_MALLOC_RESPONSE:
			dhmp_malloc_buff_response_handler(rdma_trans, msg);
			break;
		case DHMP_BUFF_MALLOC_ERROR:
			break;
		case DHMP_ACK_REQUEST:
			dhmp_ack_request_handler(rdma_trans, msg);
			break;
		case DHMP_ACK_RESPONSE:
			dhmp_ack_response_handler(rdma_trans, msg);
			break;
		case DHMP_MSG_MEM_CHANGE:
			//dhmp_mem_change_handle(rdma_trans, msg);
			break;
		case DHMP_MSG_APPLY_DRAM_REQUEST:
			//dhmp_apply_dram_request_handler(rdma_trans, msg);
			break;
		case DHMP_MSG_APPLY_DRAM_RESPONSE:
			//dhmp_apply_dram_response_handler(rdma_trans, msg);
			break;
		case DHMP_MSG_CLEAR_DRAM_REQUEST:
			//dhmp_clear_dram_request_handler(rdma_trans, msg);
			break;
		case DHMP_MSG_CLEAR_DRAM_RESPONSE:
			//dhmp_clear_dram_response_handler(rdma_trans, msg);
			break;
		case DHMP_MSG_SERVER_INFO_REQUEST:
			//dhmp_server_info_request_handler(rdma_trans, msg);
			break;
		case DHMP_MSG_SERVER_INFO_RESPONSE:
			//dhmp_server_info_response_handler(rdma_trans, msg);
			break;
		case DHMP_MSG_CLOSE_CONNECTION:
			rdma_disconnect(rdma_trans->cm_id);
			break;
	}
}