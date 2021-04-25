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


static void dhmp_malloc_request_handler(struct dhmp_transport* rdma_trans,
												struct dhmp_msg* msg)
{

	return ;
}
static void dhmp_malloc_response_handler(struct dhmp_transport* rdma_trans,
													struct dhmp_msg* msg)

{

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

	/*get server addr from dhmp_addr*/
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
			   (msg->data + sizeof(struct dhmp_send_response)), response_msg.req_info.req_size);
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