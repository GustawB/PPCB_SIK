#ifndef UDP_CLIENT_H
#define UDP_CLIENT_H

#include <inttypes.h>

void run_udp_client(struct sockaddr_in* server_addr, const char* data,
                    uint64_t data_length, uint64_t session_id);

#endif