#include "unlock_queue.h"



unQueue* initQueue(size_t entry_size, unsigned int nums)
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
        int tp_wid = q->wid;
        void * addr = (q->data + ((int)q->entry_size * tp_wid));
        memcpy(addr, data, q->entry_size);
        q->wid = (q->wid + 1) % q->len;
        return 1;
    }
}


int getQueue(unQueue *q, void * data)
{
    if(q->wid == q->rid)
        return 0;   /* 队列空 */
    else
    {
        int tp_rid = q->rid;
        void * addr = (q->data + ((int)q->entry_size * tp_rid));
        memcpy(data, addr, q->entry_size);
        q->rid = (q->rid + 1) % q->len;
        return 1;
    }
}


