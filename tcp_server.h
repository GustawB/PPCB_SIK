#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include "common.h"
#include "err.h"

#define QUEUE_LENGTH 50

void run_tcp_server(uint16_t port);

#endif