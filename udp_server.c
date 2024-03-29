#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <endian.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "udp_server.h"
#include "protconst.h"
#include "common.h"
#include "err.h"

void run_udp_server(uint16_t port) {
    // Create a socket with IPv4 protocol.
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(socket_fd < 0){
        syserr("ERROR: Failed to create a socket.");
    }

    // Bind the socket to the local adress.
    struct sockaddr_in server_addr;
    init_sockaddr(&server_addr, port);

    if (bind(socket_fd, (struct sockaddr*)&server_addr, (socklen_t) sizeof(server_addr)) < 0){
        close(socket_fd);
        syserr("ERROR: Failed to bind a socket");
    }

    // Communication loop
    for (;;) {
        // Get a CONN package.
        int flags = 0;
        struct sockaddr_in client_addr;
        socklen_t addr_length = (socklen_t)sizeof(client_addr);
        CONN connection_data;
        ssize_t bytes_read = recvfrom(socket_fd, &connection_data,
                                        sizeof(connection_data), flags,
                                        (struct sockaddr*)&client_addr,
                                        &addr_length);
        if (bytes_read < 0) {
            // Failed to establish a connection.
            close(socket_fd);
            syserr("Failed to read CONN package");
        }

        // Send CONACC back to the client.
        CONACC resp = {.pkt_type_id = CONACC_TYPE, .session_id = connection_data.session_id};
        ssize_t bytes_written = sendto(socket_fd, &resp, sizeof(resp),
                                    flags, (struct sockaddr*)&client_addr, addr_length);
        if(bytes_written < 0) {
            close(socket_fd);
            syserr("Failed to send a CONACC package.");
        }
        else if(bytes_written != sizeof(resp)) {
            close(socket_fd);
            fatal("Incomplete send.");
        }

        printf("READING DATA\n");

        // Data reading.
        uint64_t byte_count = be64toh(connection_data.data_length);
        while(byte_count > 0) {
            printf("%ld\n", byte_count);
            addr_length = (socklen_t)sizeof(client_addr);
            uint32_t curr_len = PCK_SIZE;
            if (curr_len > byte_count) {
                curr_len = byte_count;
            }

            ssize_t pck_size = sizeof(DATA) - 8 + curr_len;
            char* recv_data = malloc(pck_size);
            bytes_read = recvfrom(socket_fd, recv_data, pck_size, flags,
                                  (struct sockaddr*)&client_addr, &addr_length);
            printf("Bytes read: %ld\n", bytes_read);
            if (bytes_read < pck_size) {
                // Failed read data.
                close(socket_fd);
                syserr("Incomplete data read");
            }
            DATA* dt = (DATA*)recv_data;
            byte_count -= dt->data_size;
            printf("Received data: %s\n", recv_data + 21);
        }

        // Received a whole message, sent RCVD back to the client.
        RCVD rcvd_pck = {.pkt_type_id = RCVD_TYPE, .session_id = connection_data.session_id};
        bytes_written = sendto(socket_fd, &rcvd_pck, sizeof(rcvd_pck),
                                    flags, (struct sockaddr*)&client_addr, addr_length);
        if(bytes_written < 0) {
            close(socket_fd);
            syserr("Failed to send a RCVD package.");
        }
        else if(bytes_written != sizeof(rcvd_pck)) {
            close(socket_fd);
            fatal("Incomplete send.");
        }

        printf("Server ended\n");
    }
}