#ifndef UDP_SERVER_H
#define UDP_SERVER_H

void run_udpr_client(const struct sockaddr_in* server_addr, const char* data, 
                        uint64_t data_length, uint64_t session_id);

#endif