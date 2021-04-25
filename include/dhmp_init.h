#ifndef DHMP_INIT_H
#define DHMP_INIT_H
#include "dhmp_server.h"

struct dhmp_server * dhmp_server_init();
struct dhmp_client * dhmp_client_init(size_t size, int server_id);
#endif