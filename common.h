#ifndef COMMON_H
#define COMMON_H

#include <inttypes.h>
#include <stddef.h>
#include <sys/types.h>

#define TCP_PROT "tcp"
#define UDP_PROT "udp"
#define UDPR_PROT "udpr"

#define PCK_SIZE 64000

uint16_t read_port(const char* string);
ssize_t read_n_bytes(int fd, void* dsptr, size_t n);
ssize_t write_n_bytes(int fd, void* dsptr, size_t n);
struct sockaddr_in get_server_address(char const *host, uint16_t port);

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

#endif