#ifndef MID_API_H
#define MID_API_H

/*  不要暴露中间件buffer和DHMP命名空间中的任何东西 */

/* 旧接口，不要用 */
void* next_byte(void* addr);
char next_char(void* addr);
char get_char(void* addr);
char index_char(void* addr, int offset);
void memcpy_buffer(void* dest, void* src, int len);
int next_log(void* now_log_addr, void** next_log_addr, void** value_addr, \
                int *key_len, int *value_len);


/* 新接口 */
int read_log_key(int limits);
int read_log_value(int limits);
void pop_log();
void* top_log();
int judge_key_dirty(void * key);
void clean_log();
void send_log();

#endif