#include "dhmp.h"
#include "dhmp_transport.h"
#include "dhmp_work.h"
#include "dhmp_task.h"
#include "dhmp_client.h"
#include "dhmp_log.h"

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

	dmr->mr=ibv_reg_mr(pd, dmr->addr, length, IBV_ACCESS_LOCAL_WRITE);
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