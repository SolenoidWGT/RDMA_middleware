#ifndef MID_RDMA_UTILS_H
#define MID_RDMA_UTILS_H

#include "dhmp.h"
#include "dhmp_transport.h"


int dhmp_memory_register(struct ibv_pd *pd, 
									struct dhmp_mr *dmr, size_t length);


#endif