#ifndef DHMP_HASH_H
#define DHMP_HASH_H
#include <stddef.h>
#include <stdint.h>

typedef uint32_t (*hash_func)(const void *key, size_t length);

extern hash_func hash;

void dhmp_hash_init();

#endif

