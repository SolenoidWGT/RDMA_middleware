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
#include "dhmp_server.h"









static void dhmp_malloc_request_handler(struct dhmp_transport* rdma_trans,
												struct dhmp_msg* msg)
{
	struct dhmp_mc_response response;
	struct dhmp_msg res_msg;
	bool res=true;
	struct dhmp_device * dev;
	struct ibv_mr * mr;


	memcpy ( &response.req_info, msg->data, sizeof(struct dhmp_mc_request));
	INFO_LOG ( "client req size %d",  response.req_info.req_size);


	/* NVM 内存分配*/
	void * addr= numa_alloc_onnode(response.req_info.req_size, server->config.mem_infos->nvm_node);

	/*内存注册*/
	dev=dhmp_get_dev_from_server();
	mr= ibv_reg_mr(dev->pd,
					addr, response.req_info.req_size, 
					IBV_ACCESS_LOCAL_WRITE|
					IBV_ACCESS_REMOTE_READ|
					IBV_ACCESS_REMOTE_WRITE);
	mr->addr = addr;
	mr->length = response.req_info.req_size;
	memcpy(&response.mr, mr, sizeof(struct ibv_mr));
	DEBUG_LOG("malloc addr %p lkey %ld length is %d",
			mr->addr, mr->lkey, mr->length );
	

	res_msg.msg_type = DHMP_MSG_MALLOC_RESPONSE;
	res_msg.data_size = sizeof(struct dhmp_mc_response);
	res_msg.data= &response;
	dhmp_post_send(rdma_trans, &res_msg);
	/*count the server memory,and inform the watcher*/
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