#include "unlock_queue.h"



unQueue* initQueue(size_t entry_size, int nums)
{
    unQueue * q = (unQueue *) malloc(sizeof(unQueue));
    q->entry_size = entry_size;
    q->len = nums;
    q->rid = 0;
    q->rid = 0;
    q->data = malloc(entry_size * (nums+1));
    return q;
}


void freeQueue(unQueue * q)
{
    free(q->data);
    free(q);
}


int putQueue(unQueue * q, void * data)
{

    if( ((q->wid + 1) % q->len) == q->rid)
        return 0;   /* 队列满 */
    else
    {
        void * addr = (q->data + ((int)q->entry_size * q->wid));
        memcpy(addr, data, q->entry_size);
        q->wid = (q->wid + 1) % q->len;
        return 1;
    }
}


void popQueue(unQueue *q)
{
    if(q->wid == q->rid)
    {}
    else
        q->rid = (q->rid + 1) % q->len;
}


int emptyQueue(unQueue *q)
{
    return (q->wid == q->rid);
}

void topQueue(unQueue *q, void* buff)
{
    memcpy(buff, q->data + ((int)q->entry_size * q->rid), q->entry_size);
}