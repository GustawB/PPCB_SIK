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
        else if (bytes_read != sizeof(connection_data)) {
            error("Incomplete CONN read");
            continue;
        }

        // Set timeouts for the client.
        struct timeval time_options = {.tv_sec = MAX_WAIT, .tv_usec = 0};
        setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &time_options, sizeof(time_options));
        setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &time_options, sizeof(time_options));

        // Send CONACC back to the client.
        CONACC resp = {.pkt_type_id = CONACC_TYPE, .session_id = connection_data.session_id};
        ssize_t bytes_written = sendto(socket_fd, &resp, sizeof(resp),
                                    flags, (struct sockaddr*)&client_addr, addr_length);
        if(bytes_written < 0) {
            if (errno == EPIPE) {
                error("Server closed a connection");
            }
            else {
                close(socket_fd);
                syserr("Failed to send a CONACC package.");
            }
        }
        else if(bytes_written != sizeof(resp)) {
            error("Incomplete send.");
        }
        if(!assert_sendto(bytes_written, sizeof(resp), socket_fd)) {
            // Data reading.
            uint64_t pck_number = 0;
            uint64_t byte_count = be64toh(connection_data.data_length);
            bool b_connection_closed = false;
            while(byte_count > 0 && !b_connection_closed) {
                addr_length = (socklen_t)sizeof(client_addr);
                uint32_t curr_len = PCK_SIZE;
                if (curr_len > byte_count) {
                    curr_len = byte_count;
                }

                ssize_t pck_size = sizeof(DATA) - 8 + curr_len;
                char* recv_data = malloc(pck_size);
                bytes_read = recvfrom(socket_fd, recv_data, pck_size, flags,
                                    (struct sockaddr*)&client_addr, &addr_length);
                if (recv_data == NULL) {
                    close(socket_fd);
                    fatal("Malloc failed.");
                }

                if (connection_data.prot_id == UDPR_PROT_ID) {
                    int retransmits_counter = 1;
                    while(bytes_read < 0 && errno == EAGAIN && retransmits_counter <= MAX_RETRANSMITS) {
                        size_t bckup_resp_size = sizeof(resp);
                        if (pck_number == 0) {
                            // Send CONACC back to the client.
                            bytes_written = sendto(socket_fd, &resp, sizeof(resp),
                                                        flags, (struct sockaddr*)&client_addr, addr_length);
                        }
                        else {
                            // Send ACC back to the client.
                            ACC bckup_resp = {.pkt_type_id = ACC_TYPE, .pkt_nr = pck_number - 1, .session_id = connection_data.session_id};
                            bckup_resp_size = sizeof(bckup_resp);
                            bytes_written = sendto(socket_fd, &resp, sizeof(bckup_resp),
                                                        flags, (struct sockaddr*)&client_addr, addr_length);
                        }
                        if (errno != EAGAIN) {
                            // If something failed, it's not because of the timeout.
                            b_connection_closed = assert_sendto(bytes_written, bckup_resp_size, socket_fd);
                            if (b_connection_closed) {
                                break;
                            }
                        }
                        
                         // Managed to retransmit a package, try to read a missing data.
                        bytes_read = recvfrom(socket_fd, recv_data, pck_size, flags,
                                        (struct sockaddr*)&client_addr, &addr_length);
                        ++retransmits_counter;
                    }
                    // Exited the retransmit loop, let's check cases.
                    if (!b_connection_closed) { // Connection is still on.
                        if (retransmits_counter > MAX_RETRANSMITS) {
                            // Failed to get the missing package, end to connection.
                            b_connection_closed = true;
                            error("Failed to retrieve the missing data");
                        }
                        else {
                            close(socket_fd);
                            syserr("Failed to read data");    
                        }
                    }
                }
                else {
                    if (bytes_read < 0) {
                        if (errno == EAGAIN) {
                            b_connection_closed = true;
                            error("Connection timeout");
                        }
                        else{
                            close(socket_fd);
                            syserr("Failed to read data");
                        }
                    }
                    else if (bytes_read == 0) {
                        b_connection_closed = true;
                        error("Client closed a connection");
                    }
                    else if(bytes_read != sizeof(recv_data)) {
                        b_connection_closed = true;
                        error("Incomplete data read");
                    } 
                }

                if (!b_connection_closed) {
                    // We finally managed to get the motherfucking package.
                    DATA* dt = (DATA*)recv_data;
                    byte_count -= dt->data_size;
                    printf("Received data: %s\n", recv_data + 21);

                    if (connection_data.prot_id == UDPR_PROT_ID) {
                        // Send the ACK package.
                        ACC acc_resp = {.pkt_type_id = ACC_TYPE, .pkt_nr = pck_number, .session_id = connection_data.session_id};
                        bytes_written = sendto(socket_fd, &resp, sizeof(acc_resp),
                                                    flags, (struct sockaddr*)&client_addr, addr_length);
                        b_connection_closed = assert_sendto(bytes_written, sizeof(acc_resp), socket_fd);
                    }
                }
            }

            if(!b_connection_closed) {
                // we got all the data, now we immediately send RCVD and end the connection.
                RCVD rcvd_resp = {.pkt_type_id = RCVD_TYPE, .session_id = connection_data.session_id};
                bytes_written = sendto(socket_fd, &rcvd_resp, sizeof(rcvd_resp),
                                                    flags, (struct sockaddr*)&client_addr, addr_length);
                b_connection_closed = assert_sendto(bytes_written, sizeof(rcvd_resp), socket_fd);
            }
        }
    }

    printf("Server ended\n");
    close(socket_fd);
}