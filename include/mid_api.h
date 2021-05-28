#ifndef MID_API_H
#define MID_API_H

/*  不要暴露中间件buffer和DHMP命名空间中的任何东西 */

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
    logMateData mateData;
    int              log_pos;       // 当前log在buffer中的下标，对读写者均有用
    void*            dataAddr;      // TODO：这个域需要保留吗？
    void*            key_addr;      // 如果key被截断，则这里存放memcpy后的key指针，否则为NULL
    void*            value_addr;    // 如果value被阶段，这里存放memecpy后的value指针。否则为NULL
    /* data */
}logEntry;


/* 旧接口，不要用 */
void* next_byte(void* addr);
char next_char(void* addr);
char get_char(void* addr);
char index_char(void* addr, int offset);
void memcpy_buffer(void* dest, void* src, int len);
int next_log(void* now_log_addr, void** next_log_addr, void** value_addr, \
                int *key_len, int *value_len);


/* 新接口 */
void* get_key_addr(logEntry * log);
void* get_value_addr(logEntry * log);
int read_log_key(int limits);
int read_log_value(int limits);
void pop_log();
void* top_log();
int judge_key_dirty(void * key);

#endif