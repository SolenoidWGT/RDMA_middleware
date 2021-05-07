#include "dhmp.h"
#include "dhmp_transport.h"
#include "dhmp_work.h"
#include "dhmp_task.h"
#include "dhmp_client.h"
#include "dhmp_log.h"
#include "mid_rdma_utils.h"

// void mm()
// {
// 		/* NVM 内存分配*/
// 	void * addr= numa_alloc_onnode(response.req_info.req_size, server->config.mem_infos->nvm_node);

// 	/*内存注册*/
// 	dev=dhmp_get_dev_from_server();
// 	mr= ibv_reg_mr(dev->pd,
// 					addr, response.req_info.req_size, 
// 					IBV_ACCESS_LOCAL_WRITE|
// 					IBV_ACCESS_REMOTE_READ|
// 					IBV_ACCESS_REMOTE_WRITE);
// 	mr->addr = addr;
// 	mr->length = response.req_info.req_size;
// 	memcpy(&response.mr, mr, sizeof(struct ibv_mr));
// 	DEBUG_LOG("malloc addr %p lkey %ld length is %d",
// 			mr->addr, mr->lkey, mr->length );
// }

// dhmp_send_task_create

// dhmp_send_task_create


int dhmp_memory_register(struct ibv_pd *pd, 
									struct dhmp_mr *dmr, size_t length)
{
	dmr->addr=malloc(length);
	if(!dmr->addr)
	{
		ERROR_LOG("allocate mr memory error.");
		return -1;
	}

	dmr->mr=ibv_reg_mr(pd, dmr->addr, length,  IBV_ACCESS_LOCAL_WRITE|
												IBV_ACCESS_REMOTE_READ|
												IBV_ACCESS_REMOTE_WRITE|
												IBV_ACCESS_REMOTE_ATOMIC);
	if(!dmr->mr)	
	{
		ERROR_LOG("rdma register memory error.");
		goto out;
	}

	dmr->cur_pos=0;
	return 0;

out:
	free(dmr->addr);
	return -1;
}

struct ibv_mr * dhmp_memory_malloc_register(struct ibv_pd *pd, size_t length, int nvm_node)
{
	struct ibv_mr * mr = NULL;
	void * addr= NULL;
	addr = numa_alloc_onnode(length, nvm_node);
	// dmr->addr=malloc(length);

	if(!addr)
	{
		ERROR_LOG("allocate mr memory error.");
		return NULL;
	}
	
	mr=ibv_reg_mr(pd, addr, length, IBV_ACCESS_LOCAL_WRITE|
									IBV_ACCESS_REMOTE_READ|
									IBV_ACCESS_REMOTE_WRITE|
									IBV_ACCESS_REMOTE_ATOMIC);
	if(!mr)
	{
		ERROR_LOG("rdma register memory error.");
		goto out;
	}
	mr->addr = addr;
	mr->length = length;
	return mr;
out:
	numa_free(addr, length);
	return NULL;
}

struct dhmp_transport* dhmp_node_select_by_id(int node_id)
{
	if (client->connect_trans[node_id] != NULL &&
		(client->connect_trans[node_id]->trans_state ==
		 DHMP_TRANSPORT_STATE_CONNECTED))
		return client->connect_trans[node_id];
	return NULL;
}


int client_find_server_id()
{
	int i;
	for(i=0; i<client->config.nets_cnt; i++)
	{
		if(client->connect_trans[i] != NULL)
			return i;
	}
	return -1;
}

int find_next_node(int id)
{
	if(id >= client->config.nets_cnt-1)
		return -1;
	return  id + 1;
}