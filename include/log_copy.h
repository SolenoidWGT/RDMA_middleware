/*
 * @Descripttion: 
 * @version: 
 * @Author: sueRimn
 * @Date: 2021-05-06 09:35:21
 * @LastEditors: Please set LastEditors
 * @LastEditTime: 2021-05-18 20:55:17
 */
#ifndef LOG_COPY_H
#define LOG_COPY_H

#define TOTAL_SIZE 64 * 1024 * 1024

#include "dhmp_transport.h"
#include "mid_api.h"

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)


extern int node_class;

enum log_read_state {
	READ_NO_LOG,
    READ_LOG_KEY,
    READ_LOG_VALUE
};


typedef struct logMateData
{
    int key_length;            //  key部分长度
    int value_length;          //  data部分长度
    // char data[];                        //  key + value
}logMateData;

typedef struct logEntry
{
    logMateData mateData;      // 元数据信息
    int         log_pos;       // 当前log在buffer中的下标，对读写者均有用
    bool        key_is_cut;    // key是否截断
    bool        value_is_cut;  // value是否截断
    bool        free_cas_flag; // 释放log时的cas标志位
    void*       key_addr;      // key的连续起始地址
    void*       value_addr;    // value的连续起始地址

    /* data */
}logEntry;


// #define GET_BUFF_MR_ADDR(PTR) ( ((Ringbuff*)(PTR->addr))->buff )
// #define GET_BUFF_ADDR(PTR)  ( (Ringbuff*)(PTR->addr )) 

/*
 设定总空间为size-1，始终保留1个字节，如果剩余一个字节空间，就表示buff已满。

    先写：再移动写指针，
    后读，再移动读指针
    无锁？
*/

typedef struct Ring_buff
{
    int wr_pointer;              // 写指针cache
    int rd_pointer;               // 读指针cache
    int size;                      // 字节大小
}Ringbuff;

typedef struct Ring_buff_remote
{
    int wr_pointer;              // 写指针cache
    int rd_pointer;               // 读指针cache
    int size;                      // 字节大小
    
    int node_id;
    void* buff_mate;              // 远端buff的元数据地址
    void* buff;                  //  buff的远端地址 
}RemoteRingbuff;


typedef struct Ring_buff_local_mate
{
    struct dhmp_mr buff_mate_mr; 
}LocalMateRingbuff;

typedef struct ring_buff_local
{
    int wr_pointer;               // 写偏移量, 指向可写位置或者1字节的终点
    int rd_pointer;               // 读偏移量，指向可读位置或者1字节的终点
    int size;                     
    
    void * buff_addr;
    struct dhmp_mr buff_mr;
    int rd_key_pointer;             // 读者读取本地缓冲区key的偏移量
}LocalRingbuff;



#define REMOTE_RD_OFFSET (sizeof(int) )

#define LOCAL_WR_PTR (remote_buff->wr_pointer)
#define LOCAL_WR_PTR_ADDR (&(remote_buff->wr_pointer))

#define LOCAL_RD_PTR (local_recv_buff->rd_pointer)
#define LOCAL_RD_ADDR (local_recv_buff->buff_addr + LOCAL_RD_PTR)

#define KEY_LEN(l) ((l).mateData.key_length)
#define VALUE_LEN(l) ((l).mateData.value_length)


extern LocalRingbuff *local_recv_buff;
extern LocalMateRingbuff * local_recv_buff_mate;
extern RemoteRingbuff * remote_buff;

// tmm
extern RemoteRingbuff * slaves_buff[DHMP_SERVER_NODE_NUM];


#define POS(addr) ((void*) addr - loacl_recv_buff->buff_addr)
#define LOG_KEY_OFFSET(l)  (sizeof(logEntry))
#define LOG_VALUE_OFFSET(l)  (sizeof(logEntry) + KEY_LEN(l))
#define LOG_VALUE_TAG_OFFSET(l) ( sizeof(logEntry) + KEY_LEN(l) + VALUE_LEN(l) - 1)
#define LOG_NEXT_OFFSET(l) (sizeof(logEntry) + KEY_LEN(l) + VALUE_LEN(l))
// #define IS_CUT(addr, len) ((POS(addr) + len ) > loacl_recv_buff->size) 


// 以字节为单位进行读写
bool rb_write (void *upper_api_buf, int len);
bool rb_read (void *buf, int start, int len, bool isCopy);
void buff_init();

void* get_key_addr(logEntry * log);
void* get_value_addr(logEntry * log);



//计算数据空间大小
static inline int rb_data_size (Ringbuff *rb)    
{
    return ( (rb->wr_pointer - rb->rd_pointer) & (rb->size -1));   
}

//计算空闲空间大小
static inline int rb_free_size (Ringbuff *rb)   
{
    return ( rb->size - 1 - rb_data_size(rb));
}

//计算从读指针pos开始的空闲空间大小
static inline int rb_count_size (Ringbuff *rb, int pos)   
{
    return  ((rb->wr_pointer - pos) & (rb->size - 1));
}


static inline void update_wr_local(int new_ptr)
{
    remote_buff->wr_pointer= new_ptr;
}

static inline bool test_done(void * addr)
{
    char    test = 1;
    return !( (*(char*)addr ) ^ test);      /* 自己跟自己异或才会是0 */
}
#endif