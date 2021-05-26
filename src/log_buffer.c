/*
 * @Descripttion: 
 * @version: 
 * @Author: sueRimn
 * @Date: 2021-05-06 09:34:47
 * @LastEditors: Please set LastEditors
 * @LastEditTime: 2021-05-18 20:59:07
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
#include "mid_hashmap.h"
#include "unlock_queue.h"

#define SIZE 1024*8
#define WAIT_TIME 5
#define REPEAT 3
#define SINGLE_SIZE 1024
#define SINGLE_READ_SIZE 4
#define TAG 1

LocalRingbuff *local_recv_buff;
LocalMateRingbuff * local_recv_buff_mate;
RemoteRingbuff * remote_buff;
HashMap * dirty_map;

void * DEBUG_UPPER_BUFFER;


void rb_write_data (void *upper_api_buf, unsigned int dataPos, unsigned int dataLen);
int rb_write_mate (void *upper_api_buf, unsigned int mateLen, unsigned int dataLen);
bool rb_read (void *buf, int len, bool isCopy);
bool read_one_log(void* upperAddr);

bool check_remote_size(RemoteRingbuff *rb, int waitWriteLen)
{
    // 读远端读偏移量
    int re = dhmp_read(rb->buff_mate, &rb->rd_pointer, sizeof(int), REMOTE_RD_OFFSET, true);
    if(re == -1)
        return false;
    
    // 判断空闲空间大小
    int free_space = rb_free_size((Ringbuff *) rb);
    if(waitWriteLen > free_space)
    {
        ERROR_LOG("Remote buff of node %d 's free space is %d, \
                    smaller than len %d", rb->node_id, free_space, waitWriteLen);
        return false;
    }
    else
        return true;
}


void update_wr_remote()
{
    dhmp_write(remote_buff->buff_mate, get_wr_addr_local(), sizeof(int), 0, true);
    INFO_LOG("Success write, now wr_pointer pointer is %d", get_wr_local());   
}

char * NAME_LISTS[REPEAT];
const char * name = "wang guo teng";
unQueue* valueQueue = NULL;

void* writer_thread(void * args)
{
    int count = 0;
    pthread_detach(pthread_self());

    for(; count < REPEAT; count++)
    {
        char * ptr = (char * )malloc(SINGLE_SIZE);
        NAME_LISTS[count] = ptr;
        memset(ptr, 0 , SINGLE_SIZE);
        strcpy(ptr, name);
        ptr[0] = (char)('a' + count);
        ptr[strlen(ptr) + 1 + TAG] = 1;     // strlen 长度不包含'\0'
    }

    count = 0;
    valueQueue = initQueue(sizeof(void*), 1024);

	while(count < REPEAT)
	{
        char * key = NAME_LISTS[count];
        char * value = NAME_LISTS[count];
        size_t key_len = strlen(NAME_LISTS[count]) + 1;
        size_t value_len = strlen(NAME_LISTS[count]) + 1 + TAG;
        size_t send_len = key_len + sizeof(logEntry);

        logEntry *log = (logEntry*) malloc(sizeof(logEntry) + key_len);
        log->mateData.key_length = key_len;
        log->mateData.value_length = key_len;
        log->dataAddr = value;
        memcpy(PTR_LOG_DATA_ADDR(log), key, key_len);

        log->dataPos = rb_write_mate(log, send_len, value_len);
        
        if(log->dataPos == -1)
        {
            ERROR_LOG("rb_write_mate fail!");
        }
        else if(!putQueue(valueQueue, &log))
        {
            ERROR_LOG("valueQueue full!");
        }
        else
        {
            INFO_LOG("writer_thread write mate data of [%d] key \"%s\"", count, key);
            count ++;
        }
	}
    INFO_LOG("writer_thread write all mate data and exit!");
	pthread_exit(0);
}


void* reader_thread(void * args)
{
    int count = 0;
    char * upperBuffer;

	pthread_detach(pthread_self());
    while(local_recv_buff == NULL);
    while(local_recv_buff->buff_addr == NULL);

    /* 因为我们使用最后1字节判断是否接收完成，所以在读取完后需要将buffer重新置为全0 */
    memset(local_recv_buff->buff_addr, 0, local_recv_buff->size);
    upperBuffer = (char*) malloc(SINGLE_SIZE);
    for(;;)
    {
        if(read_one_log(upperBuffer))
        {
            count++;
            INFO_LOG("Reader has read log [%d] \"%s\"", count, upperBuffer);
            memset(upperBuffer, 0 , SINGLE_SIZE);
        }
        if(count == REPEAT)
            break;
    }
    INFO_LOG("reader_thread read all logs and exit!");
    free(upperBuffer);
    pthread_exit(0);
}

void * NIC_thread(void * args)
{
    logEntry * log= NULL;
    int count = 0;

    pthread_detach(pthread_self());
    while(valueQueue == NULL);
    for(;;)
    {
        if(getQueue(valueQueue, &log))
        {
            assert(log != NULL);
            rb_write_data(log->dataAddr, log->dataPos, PTR_VALUE_LEN(log));
            free((void*)log);  /* 不需要free掉log->data， 因为log->data实际上就是log的末尾 */
            count++;
            INFO_LOG("Writer has write log [%d] data context :\"%s\"", count, log->dataAddr);
        }
        if(count == REPEAT)
            break;
    }
    INFO_LOG("NIC_thread write all logs and exit!");
    pthread_exit(0);
}


bool read_one_log(void* upperAddr)
{
    logEntry log;

    if(!upperAddr)
        return false;

    if(rb_data_size((Ringbuff*) local_recv_buff) < sizeof(logMateData))
        return false;
    else
    {
        rb_read(&log.mateData, sizeof(logMateData), true);
        if(test_done(PTR_LOG_VALUE_TAG_ADDR(&log)))
        {
            rb_read(NULL, sizeof(logEntry) + KEY_LEN(log), false);
            rb_read(upperAddr, VALUE_LEN(log), false);
            return true;
        }
        else
            return false;
    }
}




// 写元数据，并提前移动远端写指针，预留出data的位置
int rb_write_mate (void *upper_api_buf, unsigned int mateLen, unsigned int dataLen)
{
    int totalLen = mateLen + dataLen;
    int dataPos = -1;
    if(!check_remote_size(remote_buff, totalLen))
        return -1;

    // 先写 mate 数据
    int pos = get_wr_local();
    if(pos + mateLen > remote_buff->size)
    {
        int left_size = remote_buff->size - pos;
        dhmp_write(remote_buff->buff, upper_api_buf, left_size, 0, false);
        upper_api_buf += left_size;
        mateLen -= left_size;
        pos = 0;
    }
    dhmp_write(remote_buff->buff, upper_api_buf, mateLen, pos, false);
    update_wr_local(pos+mateLen);
    dataPos = get_wr_local();

    // 再移动远端写偏移量
    pos = (get_wr_local() + dataLen) % (remote_buff->size);
    update_wr_local(pos);
    update_wr_remote();
    return dataPos;
}

// 写data数据，写到预留好的位置上去
void rb_write_data (void *upper_api_buf, unsigned int dataPos, unsigned int dataLen)
{
    // INFO_LOG("rb_write_data dataPos is %u, dataLen is %u", dataPos, dataLen);
    int pos = dataPos;
    if(pos + dataLen > remote_buff->size)
    {
        int left_size = remote_buff->size - pos;
        dhmp_write(remote_buff->buff, upper_api_buf, left_size, 0, false);
        upper_api_buf += left_size;
        dataLen -= left_size;
        pos = 0;
    }
    dhmp_write(remote_buff->buff, upper_api_buf, dataLen, pos, false);
}




// rb_read在关键路径上
bool rb_read (void *buf, int len, bool isCopy)
{
    int datasize = rb_data_size((Ringbuff*) local_recv_buff);
    if(len > datasize)
    {
        ERROR_LOG("Read len %d is bigger than dataszie %d!", len, datasize);
        return false;
    }
    int offset = 0;
    int buff_size = local_recv_buff->size;
    int pos = local_recv_buff->rd_pointer;
    if(pos + len > buff_size)
    {
        int left_size = buff_size - pos;
        if(!buf)
        {
            memcpy(buf, local_recv_buff->buff_addr, left_size);
            /* 因为我们使用最后1字节判断是否接收完成，所以在读取完后需要将buffer重新置为全0 */
            memset(local_recv_buff->buff_addr, 0 , left_size);
            buf += left_size;
        }
        len -= left_size;
        pos = 0;
    }

    if(!buf)
    {
        memcpy(buf, local_recv_buff->buff_addr + pos, len);
        memset(local_recv_buff->buff_addr + pos , 0 , len);
    }
        
    if(!isCopy)
        local_recv_buff->rd_pointer = pos + len;

    return true;
}



// rb_write不在关键路径上
bool rb_write (void *upper_api_buf, int len)
{
    if(!check_remote_size(remote_buff, len))
        return false;

    int pos = get_wr_local();
    if(pos + len > remote_buff->size) // 写入的数据加上超过循环buff大小
    {
        int left_size = remote_buff->size - pos;
        //先拷贝未超过那部分，
        dhmp_write(remote_buff->buff, upper_api_buf, left_size, 0, false);
        upper_api_buf += left_size;
        len -= left_size;
        pos = 0;
    }

    // 剩下的写入前面那部分空闲空间
    dhmp_write(remote_buff->buff, upper_api_buf, len, pos, false);
    
    // 更新远端写偏移量
    update_wr_local(pos+len);
    update_wr_remote();
    return true;
}




void buff_init()
{
    pthread_t writerForRemote, readerForLocal;
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
        DEBUG_LOG("------------------------Reader thread create---------------------------------------\n");
        DEBUG_UPPER_BUFFER = malloc(SINGLE_SIZE);
        dirty_map =  createHashMap(defaultHashCode, NULL, 1024);
        pthread_create(&readerForLocal, NULL, reader_thread, NULL);
    }
    else
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
        DEBUG_LOG("------------------------Write thread create---------------------------------------\n");
        pthread_create(&writerForRemote, NULL, writer_thread, NULL);
    }
    // void * peer_buff_mate = dhmp_malloc(sizeof(Ringbuff), client_find_server_id());
}

// void* reader_thread(void * args)
// {
// 	pthread_detach(pthread_self());
//     while(local_recv_buff == NULL);

//     unsigned int datasize = rb_data_size((Ringbuff*) local_recv_buff);
//     unsigned int offset = 0, pos;
//     unsigned int buff_size = local_recv_buff->size;
//     // unsigned int peeding_size = sizeof(logMateData);
//     enum log_read_state STATE = MID_READ_WAIT;

//     logEntry log;
//     log.data = DEBUG_UPPER_BUFFER;
//     for(;;)
//     {
//         switch (STATE)
//         {
//             case MID_READ_WAIT:
//                 while(rb_data_size((Ringbuff*) local_recv_buff) < sizeof(logMateData));
//                 rb_read(&log.mateData, sizeof(logMateData));
//                 // peeding_size = log.mateData.key_length;
//                 STATE = MID_READ_KEY;
//                 break;
//             case MID_READ_KEY:
//                 while(!test_done(log.data + KEY_LEN(log)) )
//                 rb_read(&log.data, KEY_LEN(log));
//                 // peeding_size = log.mateData.value_length;

//                 STATE = MID_READ_VALUE;
//                 break;
//             case MID_READ_VALUE:

//                 rd_read(&log.data+log.mateData.key_length, log.mateData.value_length);
//                 INFO_LOG("Reader read from remote KEY is \"%s\", value is \"%s\"", \
//                                 (char*)log.data, (char*)(log.data + log.mateData.key_length));
//                 memset(log.data, 0, log.mateData.key_length + log.mateData.value_length);
//                 // peeding_size = sizeof(logMateData);
//                 STATE = MID_READ_WAIT;
//                 break;
//             default:
//                 break;
//         }
//     }
// 	pthread_exit(0);
// }