#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <netdb.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
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

    return n = bytes_left;
}