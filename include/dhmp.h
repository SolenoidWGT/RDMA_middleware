#ifndef DHMP_H
#define DHMP_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <math.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <sys/time.h>
#include "./linux/list.h"
#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <numa.h>
#include "json-c/json.h"

#define DHMP_CACHE_POLICY
#define DHMP_MR_REUSE_POLICY

#define DHMP_SERVER_DRAM_TH ((uint64_t)1024*1024*1024*1)

#define DHMP_SERVER_NODE_NUM 10

#define DHMP_DEFAULT_SIZE 256
#define DHMP_DEFAULT_POLL_TIME 800000000

#define DHMP_MAX_OBJ_NUM 40000
#define DHMP_MAX_CLIENT_NUM 100

#define PAGE_SIZE 4096
#define NANOSECOND (1000000000)

#define DHMP_RTT_TIME (6000)
#define DHMP_DRAM_RW_TIME (260)

#define max(a,b) (a>b?a:b)
#define min(a,b) (a>b?b:a)

#ifndef bool
#define bool char
#define true 1
#define false 0
#endif



extern struct memkind * pmem_kind;

enum node_class {
	HEAD,
    NORMAL,
    TAIL
};

enum dhmp_msg_type{
	DHMP_MSG_MALLOC_REQUEST,
	DHMP_MSG_MALLOC_RESPONSE,
	DHMP_MSG_MALLOC_ERROR,
	DHMP_MSG_FREE_REQUEST,
	DHMP_MSG_FREE_RESPONSE,
	DHMP_MSG_APPLY_DRAM_REQUEST,
	DHMP_MSG_APPLY_DRAM_RESPONSE,
	DHMP_MSG_CLEAR_DRAM_REQUEST,
	DHMP_MSG_CLEAR_DRAM_RESPONSE,
	DHMP_MSG_MEM_CHANGE,
	DHMP_MSG_SERVER_INFO_REQUEST,
	DHMP_MSG_SERVER_INFO_RESPONSE,
	DHMP_MSG_CLOSE_CONNECTION,
	DHMP_MSG_SEND_REQUEST,
	DHMP_MSG_SEND_RESPONSE,

	/* WGT: add new msg type */
	DHMP_BUFF_MALLOC_REQUEST,
	DHMP_BUFF_MALLOC_RESPONSE,
	DHMP_BUFF_MALLOC_ERROR,
	DHMP_ACK_REQUEST,
	DHMP_ACK_RESPONSE,
};

enum middware_state{
	middware_INIT,
	middware_WAIT_MAIN_NODE,
	middware_WAIT_SUB_NODE,
	middware_WAIT_MATE_DATA,

};

enum request_state{
	RQ_INIT_STATE,
	RQ_BUFFER_STATE,
};

enum response_state
{
	RS_INIT_READY,
	RS_INIT_NOREADY,
	RS_BUFFER_READY,
	RS_BUFFER_NOREADY,
};

/*struct dhmp_msg:use for passing control message*/
struct dhmp_msg{
	enum dhmp_msg_type msg_type;
	size_t data_size;
	void *data;
};

/*struct dhmp_addr_info is the addr struct in cluster*/
struct dhmp_addr_info{
	int read_cnt;
	int write_cnt;
	int node_index;
	bool write_flag;
	struct ibv_mr dram_mr;
	struct ibv_mr nvm_mr;
	struct hlist_node addr_entry;
};

/*dhmp malloc request msg*/
struct dhmp_mc_request{
	size_t req_size;
	struct dhmp_addr_info *addr_info;
};

/*dhmp malloc response msg*/
struct dhmp_mc_response{
	struct dhmp_mc_request req_info;
	struct ibv_mr mr;
};

/*dhmp free memory request msg*/
struct dhmp_free_request{
	struct dhmp_addr_info *addr_info;
	struct ibv_mr mr;
};

/*dhmp free memory response msg*/
struct dhmp_free_response{
	struct dhmp_addr_info *addr_info;
};

struct dhmp_send_request{
	size_t req_size;
	void * server_addr;
	void * task;
	void * local_addr;
	bool is_write;
};

struct dhmp_send_response{
	struct dhmp_send_request req_info;
};

struct dhmp_dram_info{
	void *nvm_addr;
	struct ibv_mr dram_mr;
};



void dhmp_buff_malloc(int nodeid, void ** buff_mate_addr, void** buff_addr);

/**
 *	dhmp_malloc: remote alloc the size of length region
 */
void *dhmp_malloc(size_t length, int nodeid);

/**
 *	dhmp_read:read the data from dhmp_addr, write into the local_buf
 */
int dhmp_read(void *dhmp_addr, void * local_buf, size_t count, off_t offset, bool is_atomic);

/**
 *	dhmp_write:write the data in local buf into the dhmp_addr position
 */
int dhmp_write(void *dhmp_addr, void * local_buf, size_t length, off_t offset, bool is_atomic);

int dhmp_send(void *dhmp_addr, void * local_buf, size_t length, bool is_write);

/**
 *	dhmp_free:release remote memory
 */
void dhmp_free(void *dhmp_addr);

/**
 *	dhmp_client_init:init the dhmp client
 *	note:client connect the all server
 */
// void dhmp_client_init(size_t size, int server_id);
// void dhmp_client_mid_init(size_t size, int server_id);

/**
 *	dhmp_client_destroy:clean RDMA resources
 */
void dhmp_client_destroy();

/**
 *	dhmp_server_init:init server 
 *	include config.xml read, rdma listen,
 *	register memory, context run. 
 */
// struct dhmp_server * dhmp_server_init();

// void dhmp_server_init(struct dhmp_server * mid_server);


/**
 *	dhmp_server_destroy: close the context,
 *	clean the rdma resource
 */
void dhmp_server_destroy();


/*
 * dhmp get ack 
 */
enum response_state
dhmp_ack(int nodeid, enum request_state acktype);



int
dhmp_asyn_write(void *dhmp_addr, void * local_buf, size_t count, 
						off_t offset, bool is_atomic);

/* Middware Add New stuff is here */

/*
 * 一次dhmp标准的rpc通信过程是，主动发起请求的一方（客户端）构建request结构体，结构体中包含最终对端返回数据存放位置的指针，
 * 而接受请求的一方（服务端），构建response结构体，结构体中包含服务的返回的数据
 * 最终在客户端的recv_handler函数中将response结构体中的结果数据拷贝到request结构体给出的指针的地址处
 * 这也就是为什么response结构体里面需要包含request结构体 
 */

/*WGT: dhmp ack request request msg*/
struct dhmp_ack_request{
	int node_id;
	struct dhmp_ack_work * work;
	enum request_state  ack_flag; 
};
struct dhmp_ack_response{
	int node_id;
	struct dhmp_ack_request req_info;
	enum response_state  res_ack_flag; 
	unsigned long int log_id;
};

/*WGT: dhmp malloc Buff request msg*/
struct dhmp_buff_request{
	int node_id;
	struct dhmp_buff_malloc_work * work;
	struct dhmp_addr_info * buff_addr_info;
	struct dhmp_addr_info * buff_mate_addr_info;
};

/*WGT: dhmp malloc Buff response msg*/
struct dhmp_buff_response{
	struct dhmp_buff_request req_info;
	struct ibv_mr mr_buff;
	struct ibv_mr mr_data;
	int node_id;
};

extern pthread_mutex_t buff_init_lock; 
extern int wait_work_counter;
extern int wait_work_expect_counter;
#endif
