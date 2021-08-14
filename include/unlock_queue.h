#ifndef UNLOCK_QUEUE_H
#define UNLOCK_QUEUE_H
#include <stdio.h>
#include <stdlib.h>
#include<string.h>
typedef struct unlock_queue
{
    size_t entry_size;
    int len;
    int wid;
    int rid;
    void * data;
}unQueue;


unQueue* initQueue(size_t entry_size, int nums);
void freeQueue(unQueue * q);
int putQueue(unQueue * q, void * data);
void popQueue(unQueue *q);
int emptyQueue(unQueue *q);
void topQueue(unQueue *q, void* buff);


#endif
