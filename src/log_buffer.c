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
#include "mid_api.h"

#define SIZE 1024*8
#define WAIT_TIME 5
#define REPEAT 20
#define SINGLE_SIZE 1024
#define SINGLE_READ_SIZE 4
#define TAG 1
#define QUEUE_SIZE 2048

/* 头节点不需要缓冲区，但是要维护一个当前发送log的链表，用来判断value是否已经传输到了二号主节点 */
unQueue* wait_ack_queue = NULL;
unsigned long int global_id = 0;    // 全局自增id
unsigned long int global_read_log_nums = 0;

/* 非头节点需要缓冲区 */
LocalRingbuff *local_recv_buff = NULL;
LocalMateRingbuff * local_recv_buff_mate = NULL;
RemoteRingbuff * remote_buff = NULL;
/* 
 * Since in init stage, both main node and backup node 
 * will try to init buffer, so we need to use a lock to
 * prevent race condition.
 */
pthread_mutex_t buff_init_lock; 

RemoteRingbuff ** remote_buffs_list;  /* only for head node */

HashMap * dirty_map;
unQueue* value_peeding_queue = NULL;
unQueue* executing_queue = NULL;
unQueue* sending_queue = NULL;
unQueue* dirty_queue = NULL;
int node_class = -1;

void * DEBUG_UPPER_BUFFER;
pthread_mutex_t dirty_lock;

int wait_work_counter = 0;
int wait_work_expect_counter = 0;

void rb_write_data (void *upper_api_buf, int log_pos, int dataLen, RemoteRingbuff * targetbuff);
int rb_write_mate (void *upper_api_buf, int mateLen, int dataLen, RemoteRingbuff * targetbuff);
bool rb_read (void *buf, int start, int len, bool isCopy);
bool read_one_log(void* upperAddr);
bool main_node_write_log(char * key, char * value);

int get_node_class()
{
    if(node_class != -1)
        return node_class;
    else
    {
        ERROR_LOG("get_node_class not ready!");
        exit(0);
    }
}

void update_wr_remote(RemoteRingbuff * rb)
{
    dhmp_asyn_write(rb->buff_mate, &(rb->wr_pointer), sizeof(int), 0, true);
}

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

bool read_one_log(void* upperAddr)
{
    int datasize = 0;
    logEntry log;
    memset(&log, 0, sizeof(logEntry));
    
    if(!upperAddr)
        return false;

    datasize = rb_data_size((Ringbuff*) local_recv_buff);
    // MID_LOG("read_one_log: datasize is %d", datasize);
    if( datasize < sizeof(logMateData))
        return false;
    else
    {
        rb_read(&(log), -1, sizeof(logMateData), true);
        int offset = LOG_VALUE_TAG_OFFSET(log);
        void *addr = (void*)LOCAL_RD_ADDR + offset;
        // MID_LOG("read_one_log: key_len is %d, value_len is %d, offset is %d, addr is %p", \
        //                         KEY_LEN(log), VALUE_LEN(log), offset, addr);
        if(test_done(addr))
        {
            rb_read(NULL, -1, sizeof(logEntry) + KEY_LEN(log), false);
            rb_read(upperAddr, -1, VALUE_LEN(log), false);
            return true;
        }
        else
            return false;
    }
}

/* 
    ----------------------------------send 部分 ----------------------------------------
    send部分需要注意，头节点是没有 local_recvebuffer的，所以nic_thread在发送log的时候需要区分这种情况
*/
char * NAME_LISTS[REPEAT];
const char * name = "1234567";  // 算上末尾的终结符，一共16bit大小
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
        ptr[strlen(ptr) + TAG] = 1;     // strlen 长度不包含'\0'
    }

    count = 0;
    valueQueue = initQueue(sizeof(void*), 1024);
    MID_LOG("------------------------Write thread create---------------------------------------\n");
    wait_work_counter=0;
    wait_work_expect_counter=0;
	while(count < REPEAT)
	{
        char * key = NAME_LISTS[count];
        char * value = NAME_LISTS[count];
        if(main_node_write_log(key, value))
            count++;
	}

    MID_LOG("Main ndoe writer_thread write all mate data and exit!");
	pthread_exit(0);
}


void wait_asyn_finish()
{
    while(1)
    {
        int target = __sync_fetch_and_add(&wait_work_expect_counter, 0);
        int actual = __sync_fetch_and_add(&wait_work_counter, 0);
        MID_LOG("target is %d, actual is %d", target, actual);
        if (target == actual)
        {
            struct dhmp_work * work = NULL, * tp_wprk = NULL;
            pthread_mutex_lock(&client_mgr->mutex_asyn_work_list);
            list_for_each_entry_safe(work, tp_wprk, &client_mgr->work_asyn_list , work_entry)
            {
                struct dhmp_rw_work * wwork = (struct dhmp_rw_work *) (work->work_data);
                while(!wwork->done_flag);
                MID_LOG("dhmp_rw_work at addr %p finished", wwork->dhmp_addr);
                list_del(&work->work_entry);
                free(wwork);
                free(work);

                __sync_fetch_and_sub(&wait_work_expect_counter, 1);
                __sync_fetch_and_sub(&wait_work_counter, 1);
            }
            pthread_mutex_unlock(&client_mgr->mutex_asyn_work_list);
            break;
        }
    }
}

// 只有头节点需要负责发送元数据
// 中间节点不涉及元数据的传输
bool main_node_write_log(char * key, char * value)
{
    char test = 1;
    int value_pos;
    size_t key_len = strlen(key) + 1;
    size_t value_len = strlen(value) + 1 + TAG;
    size_t send_len = sizeof(logEntry) + key_len;
    const char ack_msg_2pc[64];
    logEntry *log = (logEntry*) malloc(send_len);

    log->mateData.key_length = key_len;
    log->mateData.value_length = value_len;
    log->mateData.id = global_id++;
    log->key_addr = NULL;       /*XXX*/
    log->value_addr = malloc(value_len);

    // 注意log里面的key同样作为 log 的 data 一部分一起发送的 
    memcpy((char*)log + LOG_KEY_OFFSET(*log), key, key_len);
    memcpy(log->value_addr, value, value_len-1);
    memcpy(log->value_addr + value_len-1, &test, TAG);

    MID_LOG("writer_thread: log id is [%d], key_len is %d, value_len is %d, total move size is %d", \
                        log->mateData.id, KEY_LEN(*log), VALUE_LEN(*log), send_len + value_len);
    
    int node_nums = server_instance->num_chain_clusters;
    int node_id;  // should be 0 + 1
    int log_pos = -1;

    for (node_id = server_instance->server_id + 1; node_id < node_nums; node_id++)
    {
        RemoteRingbuff* rbuff = remote_buffs_list[node_id];

        if(!check_remote_size(rbuff, send_len + value_len))
        {
            ERROR_LOG("remote buffer is full, please retry!");
            exit(0);
        }
    }

    for (node_id = server_instance->server_id + 1; node_id < node_nums; node_id++)
    {
        int mate_pos;
        RemoteRingbuff* rbuff = remote_buffs_list[node_id];

        if(!check_remote_size(rbuff, send_len + value_len))
        {
            ERROR_LOG("remote buffer is full, please retry!");
            exit(0);
        }

        // 这里有一个值得讨论的点，所有节点返回的 log->log_pos 是不是都是一样的
        // 理论上来说各个副本节点的最前指针的位置应该是一致的，但是由于各个节点的处理速度不同
        // 在环形缓冲区进行折返的时候，各个节点的缓冲区剩余空间不一定足够，如果遇到这种情况
        // 我们强制所有节点停止传输，以保持一致的 log->log_pos 的值
        mate_pos =  rb_write_mate(log, send_len, value_len, rbuff);
        
        if (log_pos == -1)
            log_pos = mate_pos;
        else if (mate_pos == -1 || mate_pos != log_pos)
        {
            ERROR_LOG("rb_write_mate fail!, becase of not insufficient land area");
            /* TODO: add error handler */
            exit(0);
        }
    }

    assert(log_pos != -1); 
    log->log_pos = log_pos;

    wait_asyn_finish();

    for (node_id = server_instance->server_id + 1; node_id < node_nums; node_id++)
    {
        RemoteRingbuff* rbuff = remote_buffs_list[node_id];
        update_wr_remote(rbuff);
    }

    wait_asyn_finish();

    for (node_id = server_instance->server_id + 1; node_id < node_nums; node_id++)
    {
        RemoteRingbuff* rbuff = remote_buffs_list[node_id];
        value_pos = (log->log_pos + LOG_VALUE_OFFSET(*log)) & (remote_buff->size - 1);
        rb_write_data(log->value_addr, value_pos, VALUE_LEN(*log), rbuff);
    }

    wait_asyn_finish();

    for (node_id = server_instance->server_id + 1; node_id < node_nums; node_id++)
    {
        RemoteRingbuff* rbuff = remote_buffs_list[node_id];
        dhmp_asyn_write(rbuff->buff, (void*)ack_msg_2pc, 64, rbuff->wr_pointer, false);
    }

    wait_asyn_finish();

    MID_LOG("Head_writer_thread write all data of key \"%s\"", (char*)log + LOG_KEY_OFFSET(*log));
    return true;
}

// 将log从sending queue拿出来，复用log结构体
// free_log 会把所有执行完的log发送出去
void free_log(logEntry * log)
{
    /*  因为头节点没有环形缓冲区，所以头节点不需要考虑截断的情况，也不需要移动读指针*/
    if(node_class != HEAD)
    {
        void * key_addr;
        int buff_size = local_recv_buff->size;
        /* 移动读指针 */
        local_recv_buff->rd_pointer = (local_recv_buff->rd_pointer + \
                                                LOG_NEXT_OFFSET(*log)) & (buff_size - 1);

        MID_LOG("free_log： now rd_pointer is %d", local_recv_buff->rd_pointer);
        /* TODO : 对于非头节点来说， 复用 log 结构体*/
    }
    
    /* 如果key或者value被截断，或者其是头节点的临时buffer，则将其释放 */
    if(log->key_is_cut)
        free(log->key_addr);
    if(log->value_is_cut)
        free(log->value_addr);
    free(log);     /* 对于头节点来说，不需要free掉log->key， 因为log->data实际上就是log的末尾 */
}

// NIC 只负责发送数据部分
void * NIC_thread(void * args)
{
    logEntry * log= NULL;
    int count = 0;
    int value_pos;

    pthread_detach(pthread_self());
    while(sending_queue == NULL);
    for(;;)
    {
        if(!emptyQueue(sending_queue))
        {
            topQueue(sending_queue, &log);
            assert(log != NULL);
            popQueue(sending_queue);

            value_pos = (log->log_pos + LOG_VALUE_OFFSET(*log)) & (remote_buff->size - 1);
            rb_write_data(log->value_addr, value_pos, VALUE_LEN(*log), remote_buff);

            // free_log(log);
            MID_LOG("NIC_thread has write log [%d] data context :\"%s\"", count, log->value_addr);
            count++;

            // 如果old等于sum, 就把old+1写入sum
            if(!__sync_bool_compare_and_swap(&log->free_cas_flag , false, true))
            {
                MID_LOG("NIC_thread win cas of log [%d] !", log->mateData.id);
                free_log(log);
            }
        }
        // if(count == REPEAT)
        //     break;
    }
    MID_LOG("NIC_thread write all logs and exit!");
    pthread_exit(0);
}

// 写元数据，并提前移动远端写指针，预留出data的位置
// 修改 remote_buff 应该作为一个参数传入 rb_write_mate 函数
// 头节点需要使用星型结构去写元数据
int rb_write_mate (void *upper_api_buf, int mateLen, int dataLen, RemoteRingbuff * targetbuff)
{
    int totalLen = mateLen + dataLen;
    int log_pos =  targetbuff->wr_pointer;      // 当前 log 写入位置
    // if(!check_remote_size(targetbuff, totalLen))
    //     return -1;

    // 先写 mate 数据
    int pos = targetbuff->wr_pointer;
    if(pos + mateLen > targetbuff->size)
    {
        int left_size = targetbuff->size - pos;
        dhmp_write(targetbuff->buff, upper_api_buf, left_size, pos, false);
        upper_api_buf += left_size;
        mateLen -= left_size;
        pos = 0;
    }
    dhmp_asyn_write(targetbuff->buff, upper_api_buf, mateLen, pos, false);
    update_wr_local(targetbuff, pos+mateLen);

    // 再移动远端写偏移量
    // pos = (LOCAL_WR_PTR + dataLen) % (targetbuff->size);
    pos = targetbuff->wr_pointer;
    if(pos + dataLen > targetbuff->size)
    {
        int left_size = targetbuff->size - pos;
        dataLen -= left_size;
        pos = 0;
    }
    update_wr_local(targetbuff, pos+dataLen);
//    update_wr_remote(targetbuff);
    return log_pos;
}

// 写data数据，写到预留好的位置上去
void rb_write_data (void *upper_api_buf, int log_pos, int dataLen, RemoteRingbuff * targetbuff)
{
    // MID_LOG("rb_write_data log_pos is %u, dataLen is %u", log_pos, dataLen);
    int pos = log_pos;

    if(pos + dataLen > targetbuff->size)
    {
        int left_size = targetbuff->size - pos;
        dhmp_asyn_write(targetbuff->buff, upper_api_buf, left_size, pos, false);
        upper_api_buf += left_size;
        dataLen -= left_size;
        pos = 0;
    }
    dhmp_asyn_write(targetbuff->buff, upper_api_buf, dataLen, pos, false);
}

/* 
    ----------------------------------Read 部分 ----------------------------------------
    Read部分需要注意，尾节点是没有 remote_buff r的，所以执行完log后不需要再加入到 sending_queue
*/

void* reader_thread(void * args)
{
    int count = 0;

	pthread_detach(pthread_self());
    while(local_recv_buff == NULL);
    while(local_recv_buff->buff_addr == NULL);
    MID_LOG("------------------------Reader thread create---------------------------------------\n");

    /* 因为我们使用最后1字节判断是否接收完成，所以在读取完后需要将buffer重新置为全0 */
    // memset(local_recv_buff->buff_addr, 0, local_recv_buff->size);
    for(;;)
    {
        int you_want_parse_log_nums = 1;
        int can_parse_log_nums = 0;
        
        read_log_key(1024);
        can_parse_log_nums = read_log_value(you_want_parse_log_nums);

        if(can_parse_log_nums < you_want_parse_log_nums)
        {
            // MID_LOG("can_parse_log_nums less than you_want_parse_log_nums！");
            //sleep(1);
            continue;
        }
            
        while(can_parse_log_nums > 0)
        {
            void *value_addr = top_log();
            // 将log加入到 sending 队列中
            send_log();

            /* do sommeting begin */
            MID_LOG("reader_thread has read log value is \"%s\"", value_addr);
            count++;
            /* do sommeting end */
            can_parse_log_nums--;
            clean_log();
        }
        if(count == REPEAT)
            break;
    }
    MID_LOG("reader_thread read all logs and exit!");
    pthread_exit(0);
}

void* top_log()
{
    logEntry * log;
    if(!emptyQueue(executing_queue))
    {
        topQueue(executing_queue, &log);
        return get_value_addr(log);
    }
    else
        return NULL;
}


void* get_key_addr(logEntry * log)
{
    if(log->key_addr)
        return log->key_addr;
    else
    {
        int buffer_size = local_recv_buff->size;
        int key_pos = (log->log_pos + LOG_KEY_OFFSET(*log)) & (buffer_size - 1);
        return local_recv_buff->buff_addr +  key_pos;
    }
}


void* get_value_addr(logEntry * log)
{
    if(log->value_addr)
        return log->value_addr;
    else
    {
        int buffer_size = local_recv_buff->size;
        int value_pos = (log->log_pos + LOG_VALUE_OFFSET(*log)) & (buffer_size - 1);
        return local_recv_buff->buff_addr +  value_pos;
    }
}



/*  
read_log_key
    功能：
        移动读key指针，读取元数据信息，建立logEntry结构体，将其放入到 value_peeding_queue 队列中
        同时更新脏表
    输入：
        limits: 本次读取key的最大数量
    返回值：
        读取的key的数量 
    
*/
int read_log_key(int limits)
{
    int buff_size = local_recv_buff->size;
    int read_num = 0;
    int pos = local_recv_buff->rd_key_pointer;
    for(;;)
    { 
        if(read_num >= limits)
            break;   
        else if(rb_count_size((Ringbuff*) local_recv_buff, pos) < sizeof(logMateData))
            break;
        else
        {
            logEntry * log = (logEntry *) malloc(sizeof(logEntry));
            int key_pos;

            memset(log, 0, sizeof(logEntry));
            log->log_pos = pos;     /*  log位于buffer的起始下标 */

            rb_read(log, pos, sizeof(logMateData), true);

            /*  如果key发生截断，则把key拷贝出来 */
            key_pos = (pos + LOG_KEY_OFFSET(*log)) & (buff_size - 1);
            if(key_pos + KEY_LEN(*log) >= buff_size)
            {
                log->key_addr = malloc(KEY_LEN(*log));
                log->key_is_cut = true;
                rb_read(log->key_addr, key_pos, KEY_LEN(*log), true);
                MID_LOG("read_log_key: key is cut, key is \"%s\"", log->key_addr);
            }
            else
            {
                log->key_is_cut = false;
                log->key_addr = local_recv_buff->buff_addr + key_pos;
                //MID_LOG("read_log_key: key is not cut, key is \"%s\"", log->key_addr);
            }

            pos = (pos + LOG_NEXT_OFFSET(*log)) & (buff_size -1);

            /*  必须保证key是一个c语言风格的字符串 */
            pthread_mutex_lock(&dirty_lock);
            if(!dirty_map->exists(dirty_map, log->key_addr))    
                dirty_map->put(dirty_map, log->key_addr, (void*)1);
            else
            {
                unsigned long int count;
                count = (unsigned long int)dirty_map->get(dirty_map, log->key_addr);
                dirty_map->put(dirty_map, log->key_addr, (void*)(++count));
            }
            pthread_mutex_unlock(&dirty_lock);

            /* 加入到 dirty_queue 中 */
            putQueue(dirty_queue, &log);

             /*  将logEntry放入到value peediing队列中 */
            if(!putQueue(value_peeding_queue, &log))
            {
                ERROR_LOG("value_peeding_queue is full!, log num is %d", value_peeding_queue->len);
                exit(0);
            }

            MID_LOG("read_log_key: log id is [%d], key is \"%s\", key len is %d, value len is %d", \
                        log->mateData.id, log->key_addr, log->mateData.key_length, log->mateData.value_length);

            if (log->mateData.id != global_read_log_nums)
            {
                ERROR_LOG("Unexpected log order, now log id is[%d], expected id is [%d]", log->mateData.id, global_read_log_nums);
                exit(0);
            }

            read_num++;
            global_read_log_nums++;
        }
    }
    local_recv_buff->rd_key_pointer = pos;
    return read_num;
}

int read_log_value(int limits)
{
    logEntry * log;
    void* tag_addr;
    int buffer_size = local_recv_buff->size;
    int read_num = 0;
    while(!emptyQueue(value_peeding_queue))
    {
        if(read_num >= limits)
            break;
        else
        {
            topQueue(value_peeding_queue, &log);

            tag_addr = local_recv_buff->buff_addr + ((log->log_pos + LOG_VALUE_TAG_OFFSET(*log)) & (buffer_size - 1));
            if(test_done(tag_addr))
            {
                /* 考虑value是否被截断 */
                int value_pos = ((log->log_pos + LOG_VALUE_OFFSET(*log)) & (buffer_size - 1));
                if(value_pos + VALUE_LEN(*log) >= buffer_size)
                {
                    /* value被截断，为其分配空间*/
                    log->value_addr = malloc(VALUE_LEN(*log));
                    log->value_is_cut = true;
                    rb_read(log->value_addr, value_pos, VALUE_LEN(*log), true);
                    MID_LOG("read_log_value: value is cut, value is \"%s\"", log->value_addr);
                }
                else
                {
                    log->value_is_cut = false;
                    log->value_addr = local_recv_buff->buff_addr + value_pos;
                    //MID_LOG("read_log_value: value is not cut, value is \"%s\"", log->value_addr);
                }
                
                /*  将logEntry放入到 executing_queue 队列中 */
                if(!putQueue(executing_queue, &log))
                {
                    ERROR_LOG("executing_queue is full!, log num is %d", executing_queue->len);
                    exit(0);
                }
                read_num++;
                MID_LOG("read_log_value: key is \"%s\", key len is %d, value len is %d, value is \"%s\"", \
                        log->key_addr, log->mateData.key_length, log->mateData.value_length, log->value_addr);
                
                popQueue(value_peeding_queue);   /*  将logEntry从 peedinmg value 队列中移除 */
            }
        }
    }
    return read_num;
}

void clean_log()
{
    logEntry * log;
    void * key_addr;
    int buff_size = local_recv_buff->size;
    if(!emptyQueue(dirty_queue))
    {
        topQueue(dirty_queue, &log);
        popQueue(dirty_queue);
        key_addr = get_key_addr(log);

        /* 将执行完的key从脏表中移除 */
        pthread_mutex_lock(&dirty_lock);
        if(dirty_map->exists(dirty_map, key_addr))
        {
            /* 
                即使我们释放掉count不为0的脏key字符串的内存，脏表也是可以正常工作的，
                因为key不会存储在hash表中，key字符串唯一的作用就是计算hash code
            */
            unsigned long int count = (unsigned long int) dirty_map->get(dirty_map, key_addr);
            count--;
            if(count == 0)
                dirty_map->remove(dirty_map, key_addr);
            else
                dirty_map->put(dirty_map, key_addr, (void*)count);
        }
        else
        {
            ERROR_LOG("A to be deleted log is not in the dirty map! something wrong!");
            exit(0);
        }
        pthread_mutex_unlock(&dirty_lock);

        MID_LOG("clean_log: key is \"%s\"", log->key_addr);

        /* 只有执行完后才可以释放log*/
        // 如果old等于sum, 就把old+1写入sum
        if(!__sync_bool_compare_and_swap(&log->free_cas_flag , false, true))
        {
            MID_LOG("reader_thread win cas of log [%d] !", log->mateData.id);
            free_log(log);
        }     
    }
}

// 将log从executing_queuepop出来，放入到sending queue中。
void send_log()
{
    logEntry * log;

    if(!emptyQueue(executing_queue))
    {
        topQueue(executing_queue, &log);
        popQueue(executing_queue);  /* 将其从 executing_queue 中pop掉 */

        if(node_class != TAIL)
            putQueue(sending_queue, &log);  /* 将其放入到 sending_queue 中 */
            
        // /* 加入到 dirty_queue 中 */
        // if(unlikely(dirty_queue == NULL))
        //     dirty_queue = initQueue(sizeof(void*), QUEUE_SIZE);
        
        // putQueue(dirty_queue, &log);
    }
}


/* 
    给定一个key的指针，判断其是否为脏
    key是一个c语言风格的字符串
*/
int judge_key_dirty(void * key)
{
    bool re;
    pthread_mutex_lock(&dirty_lock);
    re = dirty_map->exists(dirty_map, key);
    pthread_mutex_unlock(&dirty_lock);
    return re;
}

// rb_read在关键路径上
bool rb_read (void *buf, int start, int len, bool isCopy)
{
    int datasize = rb_data_size((Ringbuff*) local_recv_buff);
    if(len > datasize)
    {
        ERROR_LOG("Read len %d is bigger than dataszie %d!", len, datasize);
        return false;
    }
    int offset = 0;
    int buff_size = local_recv_buff->size;
    int pos = (start == -1 ) ? local_recv_buff->rd_pointer : start;
    if(pos + len > buff_size)
    {
        int left_size = buff_size - pos;
        if(buf)
        {
            memcpy(buf, local_recv_buff->buff_addr + pos, left_size);
            /* 因为我们使用最后1字节判断是否接收完成，所以在读取完后需要将buffer重新置为全0 */
            if(!isCopy)
                memset(local_recv_buff->buff_addr + pos, 0 , left_size);
            buf += left_size;
        }
        len -= left_size;
        pos = 0;
    }

    if(buf)
    {
        memcpy(buf, local_recv_buff->buff_addr + pos, len);
        if(!isCopy)
            memset(local_recv_buff->buff_addr + pos , 0 , len);
    }
        
    if(!isCopy)
        local_recv_buff->rd_pointer = pos + len;
    return true;
}

/*
// rb_write不在关键路径上
bool rb_write (void *upper_api_buf, int len)
{
    if(!check_remote_size(remote_buff, len))
        return false;

    int pos =  LOCAL_WR_PTR;
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
*/

void buff_init()
{
    value_peeding_queue = initQueue(sizeof(void*), QUEUE_SIZE);
    executing_queue = initQueue(sizeof(void*), QUEUE_SIZE);
    sending_queue = initQueue(sizeof(void*), QUEUE_SIZE);
    dirty_queue = initQueue(sizeof(void*), QUEUE_SIZE);

    if (node_class == TAIL)
    {
        // 尾节点
        MID_LOG("Node [%d] is Tail node", server_instance->server_id);
        pthread_mutex_init(&dirty_lock, NULL);
        dirty_map =  createHashMap(defaultHashCode, NULL, 1024);
    }
    else if(node_class == NORMAL)
    {
        // 初始化下游buff
        int next_node = server_instance->server_id + 1;
        MID_LOG("Next node id is %d", next_node);

        remote_buff = (RemoteRingbuff*) malloc(sizeof(RemoteRingbuff));
        memset(remote_buff, 0, sizeof(RemoteRingbuff));

        /*
         * buff_mate 是远端 buffer mate 的地址
         * buff 是远端 buffer 的起始地址
         */
        dhmp_buff_malloc(next_node, &(remote_buff->buff_mate), &(remote_buff->buff));

        if(!remote_buff->buff_mate || !remote_buff->buff)
        {
            ERROR_LOG("Get remote buff info fail!");
            exit(0);
        }

        remote_buff->node_id = next_node;
        remote_buff->size = TOTAL_SIZE;

        MID_LOG("Node [%d] get buffMate info from node [%d], buff's addr is \"%p\", size is [%d]B", \
                    server_instance->server_id, remote_buff->node_id, remote_buff->buff, remote_buff->size);
        // 中间节点
        pthread_mutex_init(&dirty_lock, NULL);
        dirty_map =  createHashMap(defaultHashCode, NULL, 1024);

    }
    else if (node_class == HEAD)
    {
        // 获得所有从节点的 buffer 元数据信息
        int node_nums = server_instance->num_chain_clusters;
        int node_id = server_instance->server_id + 1;  // should be 0 + 1
        int i = 0;
        size_t remote_buffs_list_size = sizeof(void*) * node_nums;

        remote_buffs_list = (RemoteRingbuff**) malloc(remote_buffs_list_size);
        for(i=1; i<node_nums ;i++)
        {
            remote_buffs_list[i] = (RemoteRingbuff*)malloc(sizeof(RemoteRingbuff));
            memset(remote_buffs_list[i], 0, sizeof(RemoteRingbuff));
        }

        for (; node_id < node_nums; node_id++)
        {
            RemoteRingbuff* rbuff = remote_buffs_list[node_id];
            dhmp_buff_malloc(node_id, &(rbuff->buff_mate), &(rbuff->buff));
            if(!rbuff->buff_mate || !rbuff->buff)
            {
                ERROR_LOG("Init buff fail!");
                exit(0);
            }
            rbuff->node_id = node_id;
            rbuff->size = TOTAL_SIZE;
        }

        remote_buff = remote_buffs_list[server_instance->server_id + 1];

        for(i=1; i<node_nums ;i++)
            MID_LOG("Head Node get buffMate from node [%d], buff's addr is \"%p\", size is [%d]MB", \
                    remote_buffs_list[i]->node_id, remote_buffs_list[i]->buff, remote_buffs_list[i]->size);
    }
    else
    {
        ERROR_LOG("UNkown class type!");
        exit(0);
    }

    if (node_class == HEAD)
    {
        /* Head node send siginal to anthoer nodes, ensure all nodes are ready */
        struct dhmp_transport *rdma_trans=NULL, *res_trans=NULL;

        /* ensure all node init buffe sucess */
        int i;
        for (i =0; i< DHMP_SERVER_NODE_NUM; i++)
        {
            if (client_mgr->connect_trans[i] != NULL)
            {
                enum response_state re = dhmp_ack(client_mgr->connect_trans[i]->node_id, RQ_BUFFER_STATE);
                while (re != RS_BUFFER_READY)
                {
                    re = dhmp_ack(client_mgr->connect_trans[i]->node_id, RQ_BUFFER_STATE);
                }
            }
        }

        wait_ack_queue = initQueue(sizeof(void*), QUEUE_SIZE * 10);
        MID_LOG("HEAD node[%d] init scuesss!", server_instance->server_id);
    }
}

/*  
memcpy_buffer
    功能：
        从buffer中拷贝一段内存， buffer中的内存可以不是连续的
    输入：
        dest: 拷贝目标地址
        src: 被拷贝buffer起始地址
        len: 拷贝长度
    返回值：
        len不大于可读空间返回真，小于返回假
*/
void memcpy_buffer(void* dest, void* src, int len)
{
    int pos = src - local_recv_buff->buff_addr;
    int buff_size = local_recv_buff->size;
    if(rb_count_size((Ringbuff*) local_recv_buff, pos) < len)
    {
        ERROR_LOG("memcpy_buffer out of index, dest is %p, \
                            src is %p, offset is %d", dest, src, len);
        exit(0);
    }
    if(pos + len > buff_size)
    {
        int left_size = buff_size - pos;
        memcpy(dest, local_recv_buff->buff_addr, left_size);
        dest += left_size;
        len -= left_size;
        pos = 0;
    }
    memcpy(dest, local_recv_buff->buff_addr + pos, len);
}

/*  
    get_char： 获得指定地址的char值
*/
char get_char(void* addr)
{
    return *(char*)(addr);
}

/*  
    next_char 获得addr在buffer中的下一个字节的char值
*/
char next_char(void* addr)
{
    int pos = addr - local_recv_buff->buff_addr;
    int next_pos = (pos + 1) & (local_recv_buff->size - 1);
    return *(char*)(local_recv_buff->buff_addr + next_pos);
}

/*  
    next_byte 获得addr在buffer中的下一个字节的地址
*/
void* next_byte(void* addr)
{
    int pos = addr - local_recv_buff->buff_addr;
    int next_pos = (pos + 1) & (local_recv_buff->size - 1);
    return (local_recv_buff->buff_addr + next_pos);
}

/*  
index_char
    功能：
        给一个在buffer中的地址提供类似于一维数组下标的随机访问，类似于：
            char * addr;
            char a = addr[offset];
        调用者应该保证offset不超过buffer可读范围
    输入：
        addr: 一个位于buffer中的地址
        offset: 下标偏移量
    返回值：
        addr[offset]
*/
char index_char(void* addr, int offset)
{
    int pos = addr - local_recv_buff->buff_addr;
    int buff_size = local_recv_buff->size;

    if(rb_count_size((Ringbuff*) local_recv_buff, pos) < offset)
    {
        ERROR_LOG("index_char out of index, addr is %p, offset is %d", addr, offset);
        exit(0);
    }
    pos = (pos + offset) & (buff_size - 1);
    return *(char*)(local_recv_buff->buff_addr + pos);
}

void head_node_example()
{
    for(;;)
    {
        char * key = "Key";
        char * value = "Value";
        main_node_write_log(key, value);
    }
}

void mid_node_example()
{
    int you_want_parse_log_nums = 1;
    int can_parse_log_nums = 0;
    
    read_log_key(1024);
    can_parse_log_nums = read_log_value(you_want_parse_log_nums);

    if(can_parse_log_nums < you_want_parse_log_nums)
        MID_LOG("可以解析的log数量少于希望解析的数量！");

    while(can_parse_log_nums > 0)
    {
        void *value_addr = top_log();
        send_log();
        /* do sommeting begin */

        /* do sommeting end */
        can_parse_log_nums--;
        clean_log();
    }
}

void tail_node_example()
{
    /* 和mid_node_example一样*/
}


// void* reader_thread(void * args)
// {
// 	pthread_detach(pthread_self());
//     while(local_recv_buff == NULL);

//     int datasize = rb_data_size((Ringbuff*) local_recv_buff);
//     int offset = 0, pos;
//     int buff_size = local_recv_buff->size;
//     // int peeding_size = sizeof(logMateData);
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
//                 MID_LOG("Reader read from remote KEY is \"%s\", value is \"%s\"", \
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


/*  
next_log
    功能：
        给一个log的起始地址:
            （1）如果buffer可读大小小于 matedata 大小，函数返回0，引用参数的值无意义。
            （2）如果buffer可读大小大于等于 matedata 大小：
                （2.1）如果log的value值已经发送完毕，函数返回1，
                        并通过引用参数返回下一个log的log起始地址，value起始地址，key长度和value长度。
                （2.2）如果log的value值没有发送完成，函数返回2，
                        引用参数返回值同（2.1），但是注意不能使用取value的值
        调用此函数不会移动读指针。
    输入：
        now_log_addr: 
    返回值：
        next_log_addr
        value_addr
        key_len
        value_len
        bool
*/


// int next_log(void* now_log_addr, void** next_log_addr, void** value_addr, \
//                 int *key_len, int *value_len)
// {
//     logEntry log;
//     int len = LOG_NEXT_OFFSET((logEntry*)now_log_addr);
//     int pos = now_log_addr - local_recv_buff->buff_addr;
//     int buff_size = local_recv_buff->size;
//     void * addr;

//     if(rb_count_size((Ringbuff*) local_recv_buff, pos) < len)
//     {
//         ERROR_LOG("Enter log size has some mistake!");
//         exit(0);
//     }

//     pos = (pos + len) & (buff_size - 1);

//     if(rb_count_size((Ringbuff*) local_recv_buff, pos) < sizeof(logMateData))
//         return READ_NO_LOG;
//     else
//         rb_read(&log, -1, sizeof(logMateData), true);

//     if(next_log_addr)
//         *next_log_addr = local_recv_buff->buff_addr + pos;
//     if(key_len)
//         *key_len = KEY_LEN(log);
//     if(value_len)
//         *value_len = KEY_LEN(log);
//     if(value_addr)
//         *value_addr = PTR_LOG_VALUE_ADDR(&log);

//     addr =  *next_log_addr +  LOG_VALUE_TAG_OFFSET((logEntry*) (*next_log_addr));
//     if(test_done(addr))
//         return READ_LOG_VALUE;
//     else
//         return READ_LOG_KEY;
// }

// /*  
// free_log
//     功能：
//         给一个log的起始地址，移动读指针，将这个log占用的空间从buffer释放掉
//         注意输入的log起始地址必须位于buffer的读指针起始位置
//     输入：
//         now_log_addr: 被释放的log的起始地址
// */
// void free_log_old()
// {
//     int len;
//     int pos = LOCAL_RD_PTR;
//     void* addr = local_recv_buff->buff_addr + pos;
//     if(dirty_map->get(dirty_map, PTR_LOG_DATA_ADDR(pos)))
//         dirty_map->remove(dirty_map, PTR_LOG_DATA_ADDR(pos));
//     else
//     {
//         ERROR_LOG("A to be deleted log is not in the dirty map! something wrong!");
//         exit(0);
//     }
//     len = LOG_NEXT_OFFSET((logEntry*)addr);
//     local_recv_buff->rd_pointer = (pos + len) & (local_recv_buff->size - 1);
// }