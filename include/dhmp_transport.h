#ifndef DHMP_TRANSPORT_H
#define DHMP_TRANSPORT_H
#include "dhmp_context.h"
#include "dhmp_dev.h"
#include "dhmp_work.h"
#include "dhmp_task.h"

#define ADDR_RESOLVE_TIMEOUT 500
#define ROUTE_RESOLVE_TIMEOUT 500

#define RECV_REGION_SIZE (1*1024*1024)
#define SEND_REGION_SIZE (1*1024*1024)

/*recv region include poll recv region,normal recv region*/
#define SINGLE_POLL_RECV_REGION (64*1024*1024)
#define SINGLE_NORM_RECV_REGION (64*1024)


void dhmp_comp_channel_handler(int fd, void* data);
void dhmp_wc_recv_handler(struct dhmp_transport* rdma_trans, struct dhmp_msg* msg);


enum dhmp_transport_state {
	DHMP_TRANSPORT_STATE_INIT,
	DHMP_TRANSPORT_STATE_LISTEN,
	DHMP_TRANSPORT_STATE_CONNECTING,
	DHMP_TRANSPORT_STATE_CONNECTED,
	DHMP_TRANSPORT_STATE_DISCONNECTED,
	DHMP_TRANSPORT_STATE_RECONNECT,
	DHMP_TRANSPORT_STATE_CLOSED,
	DHMP_TRANSPORT_STATE_DESTROYED,
	DHMP_TRANSPORT_STATE_ERROR,

	/* WGT: add new transport state*/
	DHMP_TRANSPORT_STATE_REJECT,
	DHMP_TRANSPORT_STATE_ADDR_ERROR,
};

struct dhmp_cq{
	struct ibv_cq	*cq;
	struct ibv_comp_channel	*comp_channel;
	struct dhmp_device *device;
	
	/*add the fd of comp_channel into the ctx*/
	struct dhmp_context *ctx;
};

struct dhmp_mr{
	size_t cur_pos;
	void *addr;
	struct ibv_mr *mr;
};

struct dhmp_transport{
	struct sockaddr_in	peer_addr;
	struct sockaddr_in	local_addr;
	
	int node_id;	/* peer node id*/
	enum dhmp_transport_state trans_state;
	struct dhmp_context *ctx;
	struct dhmp_device *device;
	struct dhmp_cq *dcq;
	struct ibv_qp *qp;
	struct rdma_event_channel *event_channel;
	struct rdma_cm_id	*cm_id;

	/*the var use for two sided RDMA*/
	struct dhmp_mr send_mr;
	struct dhmp_mr recv_mr;
	
	bool is_poll_qp;
	struct dhmp_transport *link_trans;

	long dram_used_size;
	long nvm_used_size;

	bool is_server;	// 新增的 rdma_trans 标识，如果为true则表示该 trans 是一个 server监听trans
	struct rdma_conn_param  connect_params;		/* WGT */
	enum middware_state trans_mid_state;			/* WGT: mark this trans is at which middware stage */
	struct list_head client_entry;
};

struct dhmp_send_mr{
	struct ibv_mr *mr;
	struct list_head send_mr_entry;
};


struct dhmp_cq* dhmp_cq_get(struct dhmp_device* device, struct dhmp_context* ctx);

struct dhmp_transport* dhmp_transport_create(struct dhmp_context* ctx, 
													struct dhmp_device* dev,
													bool is_listen,
													bool is_poll_qp);

// WGT
int free_trans(struct dhmp_transport* rdma_trans);


//void dhmp_transport_destroy(struct dhmp_transport *rdma_trans);


int dhmp_transport_connect(struct dhmp_transport* rdma_trans,
                             	const char* url, int port);

int dhmp_transport_listen(struct dhmp_transport* rdma_trans, int listen_port);


void dhmp_post_send(struct dhmp_transport* rdma_trans, struct dhmp_msg* msg_ptr);


/**
 *	dhmp_post_all_recv:loop call the dhmp_post_recv function
 */
void dhmp_post_all_recv(struct dhmp_transport *rdma_trans);

void dhmp_post_recv(struct dhmp_transport* rdma_trans, void *addr);



int dhmp_rdma_read_after_write ( struct dhmp_transport* rdma_trans, struct dhmp_addr_info *addr_info, \
				struct ibv_mr* mr, void* local_addr, int length);

int dhmp_rdma_write ( struct dhmp_transport* rdma_trans, struct dhmp_addr_info *addr_info, 
								struct ibv_mr* mr, void* local_addr, int length,
								off_t offset);


int dhmp_rdma_read(struct dhmp_transport* rdma_trans, struct ibv_mr* mr, void* local_addr, int length, 
						off_t offset);




#endif

