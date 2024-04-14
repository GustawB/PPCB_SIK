#include "common.h"
#include "protconst.h"
#include "tcp_client.h"
#include "udp_client.h"
#include "udpr_client.h"
#include "err.h"

int main(int argc, char* argv[]) {
    if (argc != 4){
        fatal("usage: %s <protocol> <host> <port>", argv[0]);
    }
    else if (strcmp(argv[1], TCP_PROT) != 0 && strcmp(argv[1], UDP_PROT) &&
    strcmp(argv[1], UDPR_PROT) != 0) {
        fatal("Protocol %s is not supported.", argv[1]);
    }

    // Read data from the standard input. Implemented in O(nlogn), 
    // where n is the size of the input data.
    uint64_t buffer_size = 1024;
    char* buffer = malloc(buffer_size * sizeof(char));
    assert_null(buffer, -1, -1, NULL, NULL);
    ssize_t bytes_read = 0;
    uint64_t data_length = 0;
    do {
        if (buffer_size - data_length == 0) {
            buffer_size *= 2;
            buffer = realloc(buffer, buffer_size * sizeof(char));
            assert_null(buffer, -1, -1, NULL, NULL);
        }
        bytes_read = read(STDIN_FILENO, buffer + data_length, 
                            buffer_size - data_length);
        if (bytes_read == -1) {
            free(buffer);
            syserr("Failed to read data from STDIN");
        }
        data_length += bytes_read;
    } while (bytes_read > 0);

    if(data_length != buffer_size) {
        buffer = realloc(buffer, data_length);
        assert_null(buffer, -1, -1, NULL, NULL);
    }
    
    // Generate a random session indetificator.
    time_t t;
    srand((unsigned)time(&t));
    uint64_t session_id = rand();

    // Start an appropriate server.
    const char* host_name = argv[2];
    uint16_t port = read_port(argv[3]);
    if (strcmp(argv[1], "tcp") == 0) {
        struct sockaddr_in server_addr = 
                get_server_address(host_name, port, TCP_PROT_ID);
        run_tcp_client(&server_addr, buffer, data_length, session_id);
    }
    else if (strcmp(argv[1], "udp") == 0) {
        struct sockaddr_in server_addr = 
                get_server_address(host_name, port, UDP_PROT_ID);
        run_udp_client(&server_addr, buffer, data_length, session_id);
    }
    else { // UDPR protocol.
        struct sockaddr_in server_addr = 
                get_server_address(host_name, port, UDPR_PROT_ID);
        run_udpr_client(&server_addr, buffer, data_length, session_id);
    }
    
    if (data_length > 0) {
        free(buffer);
    }

    return 0;
}