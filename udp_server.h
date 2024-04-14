#ifndef UDP_SERVER_H
#define UDP_SERVER_H

#include <arpa/inet.h>

#include "common.h"
#include "err.h"

void run_udp_server(uint16_t port);

#endif