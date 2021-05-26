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


unQueue* initQueue(size_t entry_size, unsigned int nums);
void freeQueue(unQueue * q);
int putQueue(unQueue * q, void * data);
int getQueue(unQueue *q, void * data);

#endif
