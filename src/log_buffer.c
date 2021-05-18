/*
 * @Descripttion: 
 * @version: 
 * @Author: sueRimn
 * @Date: 2021-05-06 09:34:47
 * @LastEditors: Please set LastEditors
 * @LastEditTime: 2021-05-18 19:40:46
 */


#include <stdio.h>
#include <sys/time.h>
#include "dhmp.h"
#include "dhmp_log.h"
#include "dhmp_hash.h"
#include "dhmp_config.h"
#include "dhmp_context.h"
#include "dhmp_dev.h"
#include "dhmp_transport.h"
#include "dhmp_task.h"
#include "dhmp_work.h"
#include "dhmp_client.h"
#include "dhmp_server.h"
#include "dhmp_init.h"
#include "mid_rdma_utils.h"

#include "log_copy.h"

#define SIZE 1024*8
#define WAIT_TIME 5

LocalRingbuff *local_recv_buff;
LocalMateRingbuff * local_recv_buff_mate;
RemoteRingbuff * remote_buff;


int rb_data_size (Ringbuff *rb)    //计算数据空间大小
{
    return ( (rb->wr_pointer - rb->rd_pointer) & (rb->size -1));   
}

int rb_free_size (Ringbuff *rb)   //计算空闲空间大小
{
    return ( rb->size - 1 - rb_data_size(rb));
}



// rb_read在关键路径上
int rb_read (void *buf, int len)
{
    int datasize = rb_data_size((Ringbuff*) local_recv_buff);
    if(len > datasize){
        ERROR_LOG("Read len %d is bigger than dataszie %d!");
        return datasize;
    }
    
    int offset = 0;
    int buff_size = local_recv_buff->size;
    int pos = local_recv_buff->rd_pointer;
    if(pos + len > buff_size)
    {
        int left_size = buff_size - pos;
        memcpy(local_recv_buff->buff_addr, buf, left_size);
        buf += left_size;
        len -= left_size;
        pos = 0;
    }

    memcpy(local_recv_buff->buff_addr + pos, buf, len);
    local_recv_buff->rd_pointer = pos + len;
    return datasize;
}



// rb_write不在关键路径上
int rb_write (RemoteRingbuff *rb, void *upper_api_buf, int len)
{
    // 读远端读偏移量
    dhmp_read(rb->buff_mate, &rb->rd_pointer, sizeof(int), REMOTE_RD_OFFSET, true);
    
    // 判断空闲空间大小
    int free_space = rb_free_size((Ringbuff *) rb);
    if(len > free_space)
    {
        ERROR_LOG("Remote buff of node %d 's free space is %d, smaller than len %d", rb->node_id, free_space, len);
        return free_space;
    }

    int offset = 0;
    int pos = rb->rd_pointer;
    if(pos + len > rb->size) // 写入的数据加上超过循环buff大小
    {
        int left_size = rb->size - pos;
        //先拷贝未超过那部分，
        dhmp_write(remote_buff->buff, upper_api_buf, left_size, 0, false);
        upper_api_buf += left_size;
        len -= left_size;
        pos = 0;
    }

    // 剩下的写入前面那部分空闲空间
    dhmp_write(remote_buff->buff, upper_api_buf, len, pos, false);
    
    // 更新远端写偏移量
    remote_buff->wr_pointer = pos + len;
    dhmp_write(remote_buff->buff_mate, &remote_buff->rd_pointer, sizeof(int), 0, true);
    INFO_LOG("Success write, now pointer is %d", remote_buff->rd_pointer);
    return free_space;
    
    // for(; start_b < len; start_b++)
    // {
    //     *((char *)rb->buff + start_rb) = (char *)(buf + start_b);
    //     start_rb++;
    // }
}

int atomic_update_rb_ptr(int len)
{
    remote_buff->wr_pointer = (remote_buff->wr_pointer + len) % remote_buff->size;
    int re = dhmp_write(remote_buff->buff_mate, &remote_buff->rd_pointer, sizeof(void*), 0, true);
    return re;
}


void buff_init()
{
    // 头节点
    if(server->server_id == 0)
    {
        // 初始化本地 buff

    }

    // 初始化远端buff
    remote_buff = (RemoteRingbuff*) malloc(sizeof(RemoteRingbuff));
    memset(remote_buff, 0, sizeof(RemoteRingbuff));

    int next_node = find_next_node(server->server_id);

    if( next_node == -1){
        // 尾节点
        DEBUG_LOG("Tail node is %d", server->server_id);
    }else
    {
        // 分配下游节点的buff
        DEBUG_LOG("Next node id is %d", next_node);
        dhmp_buff_malloc(next_node, &(remote_buff->buff_mate), &(remote_buff->buff));
        if(!remote_buff->buff_mate || !remote_buff->buff)
        {
            ERROR_LOG("Init buff fail!");
            exit(0);
        }
        DEBUG_LOG("Sucess malloc buff from node %d", next_node);
        remote_buff->node_id = next_node;
        remote_buff->size = BUFFER_SIZE;
    }

    // void * peer_buff_mate = dhmp_malloc(sizeof(Ringbuff), client_find_server_id());
}

