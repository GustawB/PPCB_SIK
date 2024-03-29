#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <endian.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "tcp_server.h"
#include "protconst.h"
#include "common.h"
#include "err.h"

void run_tcp_client(struct sockaddr_in* server_addr, const char* data, 
                    uint64_t data_length, uint64_t session_id) {
    // Create a socket.
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(socket_fd < 0){
        syserr("Failed to create a socket.");
    }

    // Send the CONN package.
    int flags = 0;
    socklen_t addr_length = (socklen_t)sizeof(server_addr);
    CONN connection_data = {.pkt_type_id = CONN_TYPE, .session_id = session_id,
                            .prot_id = UDP_PROT, .data_length = data_length};
    ssize_t bytes_written = sendto(socket_fd, &connection_data, sizeof(connection_data),
                                    flags, (struct sockaddr*)&server_addr, addr_length);
    if(bytes_written < 0) {
        close(socket_fd);
        syserr("Failed to send a CONN package.");
    }
    else if(bytes_written != sizeof(connection_data)) {
        close(socket_fd);
        fatal("Incomplete send.");
    }

    // Get the CONACK package.
    CONACC ack_pck;
    ssize_t bytes_read = recvfrom(socket_fd, &ack_pck,
                                    sizeof(ack_pck), flags,
                                    (struct sockaddr*)&server_addr,
                                    &addr_length);
    if (bytes_read < 0) {
        // Failed to establish a connection.
        close(socket_fd);
        syserr("Failed to read CONACC package");
    }

    printf("SENDING DAAAAAAAAAAAAAAAAAAAAAAAAAAATTTAAAAA\n");

    // Get a RCVD package and finish execution.
    RCVD rcvd_pck;
    bytes_read = recvfrom(socket_fd, &rcvd_pck,
                                    sizeof(rcvd_pck), flags,
                                    (struct sockaddr*)&server_addr,
                                    &addr_length);
    close(socket_fd);
    if (bytes_read < 0) {
        // Failed to read valid RCVD.
        syserr("Failed to read RCVD package");
    }
}