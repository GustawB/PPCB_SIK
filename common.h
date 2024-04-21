#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <inttypes.h>
#include <errno.h>
#include <unistd.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <time.h>
#include <endian.h>
#include <netdb.h>
#include <stdbool.h>
#include <limits.h>
#include <stddef.h>
#include <signal.h>

#define TCP_PROT "tcp"
#define UDP_PROT "udp"
#define UDPR_PROT "udpr"

#define TCP_PROT_ID 1
#define UDP_PROT_ID 2
#define UDPR_PROT_ID 3

#define PCK_SIZE 8

#define DEBUG_STATE 1

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

/* Utility function to read the port number from the execution args. */
uint16_t read_port(const char* string);

/* Function that reads data in loop as long as the total 
length didn't reach n. */
ssize_t read_n_bytes(int fd, void* dsptr, size_t n);
/* Function that writes data in loop as long as the total 
length didn't reach n. */
ssize_t write_n_bytes(int fd, void* dsptr, size_t n);

/* Function that initializes a package of type DATA. */
void init_data_pck(uint64_t session_id, uint64_t pck_number, 
                    uint32_t data_size, char* data_pck, const char* data);

/* Function that initializes the given sockaddr_in structure. */
void init_sockaddr(struct sockaddr_in* addr, uint16_t port);

/* Function that returns the server address based on the host, 
post and protocol (TCP, UDP or UDPR). */
struct sockaddr_in get_server_address(char const *host,
                                         uint16_t port, int8_t protocol_id);

/* Function that checks whether the write function managed to 
write to_cmp bytes. If not, passed sockets and data will be closed/freed. 
Main socket/data will be freed if any error happens, and secondary data/socket
 will be freed only if result == -1 and not because of EAGAIN.*/
bool assert_write(ssize_t result, ssize_t to_cmp, int main_fd,
                    int secondary_fd, char* main_data, char* secondary_data);
/* Function that checks whether the read function managed to read to_cmp bytes.
 If not, passed sockets and data will be closed/freed. 
Main socket/data will be freed if any error happens, and secondary data/socket
 will be freed only if result == -1 and not because of EAGAIN.*/
bool assert_read(ssize_t result, ssize_t to_cmp, int main_fd,
                    int secondary_fd, char* main_data, char* secondary_data);

/* Function that checks if the data pointer is not null.
If so, it closes/clears passed params. */
void assert_null(char* data, int main_fd, int secondary_fd,
                    char* main_data, char* secondary_data);

/* Function that tries to close the given socket and throw syserr on failure. */
assert_socket_close(int fd);

/* Function that prints len bytes of the given data. It's up to the user 
to check if len is not bigger than the data length.*/
void print_data(char* data, size_t len);

/* Function that calculates the size of the package 
to send based on the PCK_SIZE variable. */
uint32_t calc_pck_size(uint64_t data_length);

/* Function that check if the received package was CONACC from our session. */
bool get_connac_pck(const CONACC* ack_pck,  uint64_t session_id);

/* Function that check if the received package was RCVD from our session. */
bool get_nonudpr_rcvd(const RCVD* ack_pck, uint64_t session_id);

/* Function that creates socket based on the protocol_id 
(SOCK_STREAM or SOCK_DGRAM). If the operation failed, 
secondary_data will be freed. */
int create_socket(uint8_t protocol_id, char* secondary_data);

/* Function responsible for creating and binding a socket based on the
protocol_id and port. On failure, secondary_data will be cleaned */
int setup_socket(struct sockaddr_in* addr, uint8_t protocol_id, 
                    uint16_t port, char* secondary_data);

/* Function responsible for setting timeouts for the secondary_fd. 
On failure, sockets and secondary_data will be closed/cleaned. */
void set_timeouts(int main_fd, int secondary_fd, char* secondary_data);

/* Function that sets handler function as the handler fo thr given signal.
If handler is NULL, handler is set to SIG_IGN. */
void ignore_signal(void (*handler)(), int8_t signtoign);

/* Function that checks if the data size is between 1 and 64000*/
bool assert_data_size(uint32_t data_size);

#endif