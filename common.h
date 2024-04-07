#ifndef COMMON_H
#define COMMON_H

#include <inttypes.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>

#define TCP_PROT "tcp"
#define UDP_PROT "udp"
#define UDPR_PROT "udpr"

#define TCP_PROT_ID 1
#define UDP_PROT_ID 2
#define UDPR_PROT_ID 3

#define PCK_SIZE 3

#define CONN_TYPE 1
#define CONACC_TYPE 2
#define CONRJT_TYPE 3
#define DATA_TYPE 4
#define ACC_TYPE 5
#define RJT_TYPE 6
#define RCVD_TYPE 7

typedef struct __attribute__((__packed__)) {
    uint8_t pkt_type_id;
    uint64_t session_id;
    uint8_t prot_id;
    // Big endian
    uint64_t data_length;
} CONN;

typedef struct __attribute__((__packed__)) {
    uint8_t pkt_type_id;
    uint64_t session_id;
} CONACC;

typedef struct __attribute__((__packed__)) {
    uint8_t pkt_type_id;
    uint64_t session_id;
} CONRJT;

typedef struct __attribute__((__packed__)) {
    uint8_t pkt_type_id;
    uint64_t session_id;
    // Big endian
    uint64_t pkt_nr;
    // Big endian
    uint32_t data_size;
    char* data;
} DATA;

typedef struct __attribute__((__packed__)) {
    uint8_t pkt_type_id;
    uint64_t session_id;
    // Big endian
    uint64_t pkt_nr;
} ACC;

typedef struct __attribute__((__packed__)) {
    uint8_t pkt_type_id;
    uint64_t session_id;
    // Big endian
    uint64_t pkt_nr;
} RJT;

typedef struct __attribute__((__packed__)) {
    uint8_t pkt_type_id;
    uint64_t session_id;
} RCVD;

uint16_t read_port(const char* string);

ssize_t read_n_bytes(int fd, void* dsptr, size_t n);
ssize_t write_n_bytes(int fd, void* dsptr, size_t n);

void init_data_pck(uint64_t session_id, uint64_t pck_number, 
                                uint32_t data_size, char* data_pck, const char* data);

void init_sockaddr(struct sockaddr_in* addr, uint16_t port);

struct sockaddr_in get_server_address(char const *host, uint16_t port, int8_t protocol_id);

bool assert_write(ssize_t result, ssize_t to_cmp, int main_fd, int secondary_fd, char* data_to_cleanup, char* data_from_stream);
bool assert_read(ssize_t result, ssize_t to_cmp, int main_fd, int secondary_fd, char* data_to_cleanup, char* data_from_stream);

void assert_malloc(char* data, int main_fd, int secondary_fd, char* data_to_cleanup, char* data_from_stream);

void print_data(char* data, char* buffer, size_t len);

uint32_t calc_pck_size(uint64_t data_length);

ssize_t get_connac_pck(int socket_fd, const CONACC* ack_pck, 
                        ssize_t bytes_read, uint64_t session_id, char* data_from_stream);

ssize_t get_nonudpr_rcvd(int socket_fd, const RCVD* ack_pck, 
                        ssize_t bytes_read, uint64_t session_id, char* data_from_stream);

int create_socket(uint8_t protocol_id, char* data_from_stream);

int setup_socket(struct sockaddr_in* addr, uint8_t protocol_id, uint16_t port, char* data_from_stream);

void set_timeouts(int main_fd, int secondary_fd, char* data_from_stream);

#endif