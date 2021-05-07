/*
 * @Descripttion: 
 * @version: 
 * @Author: sueRimn
 * @Date: 2021-05-06 09:35:21
 * @LastEditors: sueRimn
 * @LastEditTime: 2021-05-07 16:17:36
 */
#ifndef LOG_COPY_H
#define LOG_COPY_H

#define TOTAL_SIZE 1024
#define BUFFER_SIZE (TOTAL_SIZE - 1)

#include "dhmp_transport.h"


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
}LocalRingbuff;



#define SIZE_INT (sizeof(int))
#define REMOTE_RD_OFFSET (SIZE_INT )

// #define REMOTE_WR_ADDR(PTR) (PTR->buff_mate)
// #define REMOTE_RD_ADDR(PTR) (PTR->buff_mate + SIZE_INT )
// #define REMOTE_SIZE(PTR) (PTR->buff_mate + 2 * SIZE_INT )


// #define PUSH_ADDR (PTR) (PTR->buff + START_REMOTE_WR_PTR(PTR) )
// #define POP_ADDR (PTR) (PTR->buff + START_REMOTE_RD_PTR(PTR) )

// typedef struct remote_buff
// {
//     void* buff_mate;              
//     void* buff;
// }RemoteRingbuff;





typedef struct log_entry
{
    unsigned int key_length;            //  key部分长度
    unsigned int value_length;          //  data部分长度
    char data[];                        //  key + value
};


extern LocalRingbuff *local_recv_buff;
extern LocalMateRingbuff * local_recv_buff_mate;
extern RemoteRingbuff * remote_buff;

// 以字节为单位进行读写
int rb_write (RemoteRingbuff *rb, void *buf, int len);
int rb_read (void *buf, int len);
void buff_init();


#endif