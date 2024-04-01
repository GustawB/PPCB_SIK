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

#include "udp_server.h"
#include "protconst.h"
#include "common.h"
#include "err.h"

void run_udp_client(const struct sockaddr_in* server_addr, const char* data, 
                    uint64_t data_length, uint64_t session_id) {
    printf("Started udp client\n");
    // Create a socket.
    // Using server_addr directly caused problems, so I'm performing
    // a local copy of the sockaddr_in structure.
    struct sockaddr_in loc_server_addr = *server_addr;
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(socket_fd < 0){
        syserr("Failed to create a socket.");
    }

    // Set timeouts for the server.
    //struct timeval time_options = {.tv_sec = MAX_WAIT, .tv_usec = 0};
    //setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &time_options, sizeof(time_options));
    //setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &time_options, sizeof(time_options));

    // Send the CONN package.
    int flags = 0;
    socklen_t addr_length = (socklen_t)sizeof(*server_addr);
    CONN connection_data = {.pkt_type_id = CONN_TYPE, .session_id = session_id,
                            .prot_id = UDP_PROT_ID, .data_length = htobe64(data_length)};
    ssize_t bytes_written = sendto(socket_fd, &connection_data, sizeof(connection_data),
                                    flags, (struct sockaddr*)&loc_server_addr, addr_length);
    bool b_connection_closed = assert_sendto(bytes_written, sizeof(connection_data), socket_fd);

    if (!b_connection_closed) {
        // Get the CONACC package.
        CONACC ack_pck;
        ssize_t bytes_read = recvfrom(socket_fd, &ack_pck,
                                        sizeof(ack_pck), flags,
                                        (struct sockaddr*)&server_addr,
                                        &addr_length);
        if (bytes_read < 0) {
            // Failed to establish a connection.
            close(socket_fd);
            syserr("Recvfrom failed");
        }
        else if (bytes_read != sizeof(ack_pck)) {
            close(socket_fd);
            fatal("Invalid package");
        }

        // Send data to the server.
        uint64_t pck_number = 0;
        const char* data_ptr = data;
        while(data_length > 0 && !b_connection_closed) {
            // recvfrom can change the value of th eaddr_length,
            // so I have to update it here over and over again.
            addr_length = (socklen_t)sizeof(loc_server_addr);
            uint32_t curr_len = PCK_SIZE;
            if (curr_len > data_length) {
                curr_len = data_length;
            }

            // Initialize a package.
            ssize_t pck_size = sizeof(DATA) - 8 + curr_len;
            char* data_pck = malloc(pck_size);
            if (data_pck == NULL) {
                close(socket_fd);
                fatal("Malloc failed");
            }

            init_data_pck(session_id, pck_number, 
                                    curr_len, data_pck, data_ptr);
            bytes_written = sendto(socket_fd, data_pck, pck_size, flags,
                                    (struct sockaddr*)&loc_server_addr, addr_length);
            b_connection_closed = assert_sendto(bytes_written, pck_size, socket_fd);

            printf("Pck number: %ld\n", pck_number);
            ++pck_number;
            data_length -= curr_len;
            data_ptr += curr_len;
        }

        if (!b_connection_closed) {
            // Get a RCVD package and finish execution.
            RCVD rcvd_pck;
            bytes_read = recvfrom(socket_fd, &rcvd_pck,
                                            sizeof(rcvd_pck), flags,
                                            (struct sockaddr*)&loc_server_addr,
                                            &addr_length);
            if (bytes_read < 0) {
                close(socket_fd);
                syserr("Recvfrom failed");
            }
            else if (bytes_read != sizeof(rcvd_pck)) {
                close(socket_fd);
                syserr("Invalid package");
            }
        }
    }

    close(socket_fd);
    printf("Client ended\n");
}