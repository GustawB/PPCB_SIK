#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <endian.h>
#include <inttypes.h>
#include <netdb.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>

#include "protconst.h"
#include "tcp_client.h"
#include "udp_client.h"
#include "udpr_client.h"
#include "common.h"
#include "err.h"

int main(int argc, char* argv[]) {
    if (argc != 4){
        fatal("usage: %s <protocol> <host> <port>", argv[0]);
    }
    else if (strcmp(argv[1], TCP_PROT) != 0 && strcmp(argv[1], UDP_PROT) &&
    strcmp(argv[1], UDPR_PROT) != 0) {
        fatal("Protocol %s is not supported.", argv[1]);
    }

    // Read data from the standard input.
    char* line = NULL;
    char* data = NULL;
    size_t n = 0;
    ssize_t bytes_read = 0;
    uint64_t data_length = 0;
    // Read lines from standard input until EOF is encountered
    while ((bytes_read = getline(&line, &n, stdin)) != -1) {
        // Process each line here
        data_length += bytes_read;
        char* tmp = malloc(data_length);
        if (tmp == NULL) {
            free(line);
            free(data);
            fatal("Malloc failed");
        }
        memcpy(tmp, data, data_length - bytes_read);
        free(data);
        data = tmp;
        tmp += data_length - bytes_read;
        memcpy(tmp, line, bytes_read);
        free(line);
        line = NULL;
        free(line);
        line = NULL;
    }
    free(line);
    if (errno != 0) {
        free(data);
        fatal("Geline failed");
    }
    
    // Generate a random session indetificator.
    time_t t;
    srand((unsigned)time(&t));
    uint64_t session_id = rand();

    // Start the appropriate server.
    const char* host_name = argv[2];
    uint16_t port = read_port(argv[3]);
    if (strcmp(argv[1], "tcp") == 0) {
        struct sockaddr_in server_addr = get_server_address(host_name, port, TCP_PROT_ID);
        run_tcp_client(&server_addr, data, data_length, session_id);
    }
    else if (strcmp(argv[1], "udp") == 0) {
        struct sockaddr_in server_addr = get_server_address(host_name, port, UDP_PROT_ID);
        run_udp_client(&server_addr, data, data_length, session_id);
    }
    else { // UDPR protocol.
        struct sockaddr_in server_addr = get_server_address(host_name, port, UDPR_PROT_ID);
        run_udpr_client(&server_addr, data, data_length, session_id);
    }
    
    if (data_length > 0) {
        free(data);
    }

    return 0;
}