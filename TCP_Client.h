#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <inttypes.h>

void run_tcp_client(struct sockaddr_in* server_addr, const char* data,
                    uint64_t data_length, uint64_t session_id);

#endif