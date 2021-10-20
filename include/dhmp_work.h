/*
 * @Descripttion: 
 * @version: 
 * @Author: sueRimn
 * @Date: 2021-04-25 17:46:17
 * @LastEditors: Please set LastEditors
 * @LastEditTime: 2021-05-07 00:32:34
 */
#ifndef DHMP_WORK_H
#define DHMP_WORK_H
#include "dhmp.h"
#include "dhmp_transport.h"

//WGT
struct dhmp_rw_work{
	struct dhmp_transport *rdma_trans;
	void *dhmp_addr;
	void *local_addr;
	struct dhmp_send_mr *smr;
	size_t length;
	bool done_flag;
	off_t offset;	
	bool is_atomic;
};

struct dhmp_malloc_work{
	struct dhmp_transport *rdma_trans;
	struct dhmp_addr_info *addr_info;
	void *res_addr;
	size_t length;
	bool done_flag;
};


/*WGT: dhmp malloc Buff work*/
struct dhmp_buff_malloc_work{
	/* transposrt_mate_data header */

	struct dhmp_transport *rdma_trans;
	/* Data section begin */
	struct dhmp_addr_info * buff_addr_info;
	struct dhmp_addr_info * buff_mate_addr_info;
	void * buff_addr;
	void * buff_mate_addr;
	/* Data section end */

	int node_id;
	/* done_flag should place in the end of struct(? )*/
	bool done_flag_recv;
	bool done_flag;
};

/*WGT: dhmp ACK work, only send ack flag*/
struct dhmp_ack_work{
	struct dhmp_transport *rdma_trans;
	enum request_state  ack_flag; 
	enum response_state  res_ack_flag; 
	bool done_flag;
	bool done_flag_recv;
};

struct dhmp_free_work{
	struct dhmp_transport *rdma_trans;
	void *dhmp_addr;
	bool done_flag;
};

struct dhmp_close_work{
	struct dhmp_transport *rdma_trans;
	bool done_flag;
};

struct dhmp_send_work{
	struct dhmp_transport *rdma_trans;
	size_t length;
	bool done_flag;
	void * dhmp_addr;
	void * local_addr;
	bool recv_flag;
	bool is_write; //or read
};

enum dhmp_work_type{
	DHMP_WORK_MALLOC,
	DHMP_WORK_FREE,
	DHMP_WORK_READ,
	DHMP_WORK_WRITE,
	DHMP_WORK_POLL,
	DHMP_WORK_CLOSE,
	DHMP_WORK_DONE,
	DHMP_WORK_SEND,

	/* WGT: add new work type */
	DHMP_BUFF_MALLOC,
	DHMP_WORK_ACK
};

struct dhmp_work{
	enum dhmp_work_type work_type;
	void *work_data;
	struct list_head work_entry;
};

void *dhmp_work_handle_thread(void *data);
int dhmp_hash_in_client(void *addr);
void *dhmp_transfer_dhmp_addr(struct dhmp_transport *rdma_trans,
									void *normal_addr);
struct dhmp_addr_info *dhmp_get_addr_info_from_ht(int index, void *dhmp_addr);

#endif



