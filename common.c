#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <netdb.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "err.h"
#include "common.h"

uint16_t read_port(char const *string) {
    char *endptr;
    unsigned long port = strtoul(string, &endptr, 10);
    if ((port == ULONG_MAX && errno == ERANGE) || *endptr != 0 || 
        port == 0 || port > UINT16_MAX) {
        fatal("%s is not a valid port number.", string);
    }
    return (uint16_t) port;
}

ssize_t read_n_bytes(int fd, void* dsptr, size_t n) {
    ssize_t bytes_read;
    ssize_t bytes_left = n;
    char* iter_ptr = dsptr;

    // Read stream as long as there is no error and we didn't read everything. 
    while (bytes_left > 0) {
        if ((bytes_read = read(fd, iter_ptr, bytes_left)) < 0) {
            // read() failed, return < 0.
            return bytes_read;
        }
        else if (bytes_read == 0) {
            // Encountered EOF.
            break;
        }

        bytes_left -= bytes_read;
        iter_ptr += bytes_read;
    }

    return n - bytes_left;
}

ssize_t write_n_bytes(int fd, void* dsptr, size_t n) {
    ssize_t bytes_written;
    ssize_t bytes_left = n;
    const char* iter_ptr = dsptr;

    // Write to the stream as long as there is no error and we didn't write everything.
    while(bytes_left > 0) {
        if ((bytes_written = write(fd, iter_ptr, bytes_left)) <= 0) {
            // There was some kind of error.
            return bytes_written;
        }

        bytes_left -= bytes_written;
        iter_ptr += bytes_written;
    }

    return n - bytes_left;
}

struct sockaddr_in get_server_address(char const *host, uint16_t port) {
    struct addrinfo hints;
    // Malloc would require me to worry about freeing the memory later,
    // and I don't need pointer arithmetics.
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* addr_res;
    int errcode = getaddrinfo(host, NULL, &hints, &addr_res);
    if (errcode != 0) {
        fatal("getaddrinfo: %s", gai_strerror(errcode));
    }

    struct sockaddr_in send_addr;
    send_addr.sin_family = AF_INET; // IPv4;
    // IP address.
    send_addr.sin_addr.s_addr = 
        ((struct sockaddr_in*)addr_res->ai_addr)->sin_addr.s_addr;
    send_addr.sin_port = htons(port);

    freeaddrinfo(addr_res);

    return send_addr;
}