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
#define REPEAT 3
#define SINGLE_SIZE 1024
#define SINGLE_READ_SIZE 4
#define TAG 1
#define QUEUE_SIZE 2048

LocalRingbuff *local_recv_buff;
LocalMateRingbuff * local_recv_buff_mate;
RemoteRingbuff * remote_buff;
HashMap * dirty_map;
unQueue* value_peeding_queue = NULL;
unQueue* executing_queue = NULL;

void * DEBUG_UPPER_BUFFER;


void rb_write_data (void *upper_api_buf, int log_pos, int dataLen);
int rb_write_mate (void *upper_api_buf, int mateLen, int dataLen);
bool rb_read (void *buf, int start, int len, bool isCopy);
bool read_one_log(void* upperAddr);

void update_wr_remote()
{
    dhmp_write(remote_buff->buff_mate, LOCAL_WR_PTR_ADDR, sizeof(int), 0, true);
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
        ptr[strlen(ptr) + TAG] = 1;     // strlen 长度不包含'\0'
    }

    count = 0;
    valueQueue = initQueue(sizeof(void*), 1024);
    DEBUG_LOG("------------------------Write thread create---------------------------------------\n");
	while(count < REPEAT)
	{
        char * key = NAME_LISTS[count];
        char * value = NAME_LISTS[count];
        size_t key_len = strlen(NAME_LISTS[count]) + 1;
        size_t value_len = strlen(NAME_LISTS[count]) + 1 + TAG;
        size_t send_len = key_len + sizeof(logEntry);

        logEntry *log = (logEntry*) malloc(send_len);
        log->mateData.key_length = key_len;
        log->mateData.value_length = value_len;
        log->dataAddr = value;
        memcpy(log + LOG_KEY_OFFSET(*log), key, key_len);

        INFO_LOG("writer_thread: key_len is %d, value_len is %d, total move size is %d", \
                                KEY_LEN(*log), VALUE_LEN(*log), send_len + value_len);

        log->log_pos = rb_write_mate(log, send_len, value_len);
        
        if(log->log_pos == -1)
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

void * NIC_thread(void * args)
{
    logEntry * log= NULL;
    int count = 0;

    pthread_detach(pthread_self());
    while(valueQueue == NULL);
    for(;;)
    {
        if(!emptyQueue(valueQueue))
        {
            assert(log != NULL);
            topQueue(valueQueue, &log);
            popQueue(valueQueue);

            int value_pos = (log->log_pos + LOG_VALUE_OFFSET(*log)) & (remote_buff->size - 1);
            rb_write_data(log->dataAddr, value_pos, VALUE_LEN(*log));
            free((void*)log);  /* 不需要free掉log->data， 因为log->data实际上就是log的末尾 */
            count++;
            INFO_LOG("NIC_thread has write log [%d] data context :\"%s\"", count, log->dataAddr);
        }
        if(count == REPEAT)
            break;
    }
    INFO_LOG("NIC_thread write all logs and exit!");
    pthread_exit(0);
}


// 写元数据，并提前移动远端写指针，预留出data的位置
int rb_write_mate (void *upper_api_buf, int mateLen, int dataLen)
{
    int totalLen = mateLen + dataLen;
    int log_pos =  LOCAL_WR_PTR;
    if(!check_remote_size(remote_buff, totalLen))
        return -1;

    // 先写 mate 数据
    int pos = LOCAL_WR_PTR;
    if(pos + mateLen > remote_buff->size)
    {
        int left_size = remote_buff->size - pos;
        dhmp_write(remote_buff->buff, upper_api_buf, left_size, pos, false);
        upper_api_buf += left_size;
        mateLen -= left_size;
        pos = 0;
    }
    dhmp_write(remote_buff->buff, upper_api_buf, mateLen, pos, false);
    update_wr_local(pos+mateLen);
    

    // 再移动远端写偏移量
    // pos = (LOCAL_WR_PTR + dataLen) % (remote_buff->size);
    pos = LOCAL_WR_PTR;
    if(pos + dataLen > remote_buff->size)
    {
        int left_size = remote_buff->size - pos;
        dataLen -= left_size;
        pos = 0;
    }
    update_wr_local(pos+dataLen);
    update_wr_remote();
    return log_pos;
}

// 写data数据，写到预留好的位置上去
void rb_write_data (void *upper_api_buf, int log_pos, int dataLen)
{
    // INFO_LOG("rb_write_data log_pos is %u, dataLen is %u", log_pos, dataLen);
    int pos = log_pos;

    if(pos + dataLen > remote_buff->size)
    {
        int left_size = remote_buff->size - pos;
        dhmp_write(remote_buff->buff, upper_api_buf, left_size, pos, false);
        upper_api_buf += left_size;
        dataLen -= left_size;
        pos = 0;
    }
    dhmp_write(remote_buff->buff, upper_api_buf, dataLen, pos, false);
}


void* reader_thread(void * args)
{
    int count = 0;
    char * upperBuffer;

	pthread_detach(pthread_self());
    while(local_recv_buff == NULL);
    while(local_recv_buff->buff_addr == NULL);
    DEBUG_LOG("------------------------Reader thread create---------------------------------------\n");

    /* 因为我们使用最后1字节判断是否接收完成，所以在读取完后需要将buffer重新置为全0 */
    // memset(local_recv_buff->buff_addr, 0, local_recv_buff->size);
    upperBuffer = (char*) malloc(SINGLE_SIZE);
    for(;;)
    {
        if(read_one_log(upperBuffer))
        {
            INFO_LOG("Reader has read log [%d] \"%s\"", count, upperBuffer);
            memset(upperBuffer, 0 , SINGLE_SIZE);
            count++;
        }
        if(count == REPEAT)
            break;
    }
    INFO_LOG("reader_thread read all logs and exit!");
    free(upperBuffer);
    pthread_exit(0);
}

bool read_one_log(void* upperAddr)
{
    int datasize = 0;
    logEntry log;
    memset(&log, 0, sizeof(logEntry));
    
    if(!upperAddr)
        return false;

    datasize = rb_data_size((Ringbuff*) local_recv_buff);
    // INFO_LOG("read_one_log: datasize is %d", datasize);
    if( datasize < sizeof(logMateData))
        return false;
    else
    {
        rb_read(&(log), -1, sizeof(logMateData), true);
        int offset = LOG_VALUE_TAG_OFFSET(log);
        void *addr = (void*)LOCAL_RD_ADDR + offset;
        // INFO_LOG("read_one_log: key_len is %d, value_len is %d, offset is %d, addr is %p", \
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


void example()
{
    int you_want_parse_log_nums = 1;
    int can_parse_log_nums = 0;
    
    read_log_key(1024);
    can_parse_log_nums = read_log_value(you_want_parse_log_nums);

    if(can_parse_log_nums < you_want_parse_log_nums)
        INFO_LOG("可以解析的log数量少于希望解析的数量！");

    while(can_parse_log_nums > 0)
    {
        void *value_addr = top_log();

        /* do sommeting begin */

        /* do sommeting end */
        can_parse_log_nums--;
        pop_log();
    }
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
            if(key_pos + KEY_LEN(*log) > buff_size)
            {
                log->key_addr = malloc(KEY_LEN(*log));
                rb_read(log->key_addr, key_pos, KEY_LEN(*log), true);
            }
            
            pos = (pos + LOG_NEXT_OFFSET(*log)) & (buff_size -1);

            /*  必须保证key是一个c语言风格的字符串 */
            if(!dirty_map->exists(dirty_map, log->key_addr))    
                dirty_map->put(dirty_map, log->key_addr, (void*)1);
            else
            {
                unsigned long int count;
                count = (unsigned long int)dirty_map->get(dirty_map, log->key_addr);
                dirty_map->put(dirty_map, log->key_addr, (void*)(++count));
            }

            if(value_peeding_queue == NULL)
                value_peeding_queue = initQueue(sizeof(void*), QUEUE_SIZE);

             /*  将logEntry放入到value peediing队列中 */
            if(!putQueue(value_peeding_queue, &log))
            {
                ERROR_LOG("value_peeding_queue is full!, log num is %d", value_peeding_queue->len);
                exit(0);
            }
            read_num++;
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
                if(value_pos + VALUE_LEN(*log) > buffer_size)
                {
                    /* value被截断，为其分配空间*/
                    log->value_addr = malloc(VALUE_LEN(*log));
                    rb_read(log->value_addr, value_pos, VALUE_LEN(*log), true);
                }

                if(executing_queue == NULL)
                    executing_queue = initQueue(sizeof(void*), QUEUE_SIZE);

                /*  将logEntry放入到 executing_queue 队列中 */
                if(!putQueue(executing_queue, &log))
                {
                    ERROR_LOG("executing_queue is full!, log num is %d", executing_queue->len);
                    exit(0);
                }
                
                /*  将logEntry从 peedinmg value 队列中移除 */
                popQueue(value_peeding_queue);
                read_num++;
            }
            else
                break;
        }
    }
    return read_num;
}


void pop_log()
{
    logEntry * log;
    void * key_addr;
    int buff_size = local_recv_buff->size;

    if(!emptyQueue(executing_queue))
    {
        topQueue(executing_queue, &log);
        key_addr = get_key_addr(log);

        /* 将执行完的key从脏表中移除 */
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

        /* 移动读指针 */
        local_recv_buff->rd_pointer = (local_recv_buff->rd_pointer + \
                                             LOG_NEXT_OFFSET(*log)) & (buff_size - 1);
        
        /* 将其从 executing_queue 中pop掉 */
        popQueue(executing_queue);

        /* 如果key或者value被截断，则将其释放 */
        if(log->key_addr)
            free(log->key_addr);
        if(log->value_addr)
            free(log->value_addr);

        free(log);
    }
}


/* 
    给定一个key的指针，判断其是否为脏
    key是一个c语言风格的字符串
*/
int judge_key_dirty(void * key)
{
    return dirty_map->exists(dirty_map, key);
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
            memcpy(buf, local_recv_buff->buff_addr, left_size);
            /* 因为我们使用最后1字节判断是否接收完成，所以在读取完后需要将buffer重新置为全0 */
            if(!isCopy)
                memset(local_recv_buff->buff_addr, 0 , left_size);
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




void buff_init()
{
    pthread_t writerForRemote, readerForLocal, nic_thead;
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
        remote_buff->size = TOTAL_SIZE;
        pthread_create(&writerForRemote, NULL, writer_thread, NULL);
        pthread_create(&nic_thead, NULL, NIC_thread, NULL);
    }
    // void * peer_buff_mate = dhmp_malloc(sizeof(Ringbuff), client_find_server_id());
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