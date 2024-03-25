#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <inttypes.h>

void run_tcp_client(const struct sockaddr_in* server_addr);

#endif