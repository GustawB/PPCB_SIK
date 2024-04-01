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

    // Set timeouts for the client.
    struct timeval time_options = {.tv_sec = MAX_WAIT, .tv_usec = 0};
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &time_options, sizeof(time_options));
    setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &time_options, sizeof(time_options));

    // Communication loop
    for (;;) {
        printf("Start communication loop\n");
        // Get a CONN package.
        int flags = 0;
        struct sockaddr_in client_addr;
        socklen_t addr_length = (socklen_t)sizeof(client_addr);
        CONN connection_data;
        bool b_connection_closed = false;
        while(!b_connection_closed) {
            ssize_t bytes_read = recvfrom(socket_fd, &connection_data,
                                        sizeof(connection_data), flags,
                                        (struct sockaddr*)&client_addr,
                                        &addr_length);
            
            if ((bytes_read < 0 && errno != EAGAIN) || bytes_read == 0) {
                b_connection_closed = assert_recvfrom(bytes_read, sizeof(connection_data), socket_fd);
            }
            else  if (bytes_read > 0) {
                printf("Bytes read: %ld\n", bytes_read);
                // We read SOMETHING.
                if (connection_data.pkt_type_id == CONN_TYPE) {
                    // We got what we wanted.
                    printf("Fucker connected with us: %ld\n", connection_data.session_id);
                    break;
                }
                // Otherwise, we got something else, ignore the fucker.
            }
        }

        // Send CONACC back to the client.
        CONACC resp = {.pkt_type_id = CONACC_TYPE, .session_id = connection_data.session_id};
        ssize_t bytes_written = sendto(socket_fd, &resp, sizeof(resp),
                                    flags, (struct sockaddr*)&client_addr, addr_length);
        b_connection_closed = assert_sendto(bytes_written, sizeof(resp), socket_fd);

        printf("Server sent CONACC: %ld\n", resp.session_id);
        printf("Should sent: %ld\n", connection_data.session_id);

        if(!b_connection_closed) {
            // If we managed to send the CONACC, read the data.
            uint64_t pck_number = 0;
            uint64_t byte_count = be64toh(connection_data.data_length);
            while(byte_count > 0 && !b_connection_closed) {
                addr_length = (socklen_t)sizeof(client_addr);
                uint32_t curr_len = PCK_SIZE;
                if (curr_len > byte_count) {
                    curr_len = byte_count;
                }

                ssize_t pck_size = sizeof(DATA) - 8 + curr_len;
                char* recv_data = malloc(pck_size);
                if (recv_data == NULL) {
                    close(socket_fd);
                    syserr("Malloc failed");
                }
                printf("Receiving data\n");
                ssize_t bytes_read = recvfrom(socket_fd, recv_data, pck_size, flags,
                                    (struct sockaddr*)&client_addr, &addr_length);
                if (connection_data.prot_id == UDPR_PROT_ID) {
                    int retransmits_counter = 0;
                    // Try to get the data.
                    while(!b_connection_closed) {
                        if ((bytes_read < 0 && errno != EAGAIN) || bytes_read == 0) {
                            b_connection_closed = assert_recvfrom(bytes_read, sizeof(connection_data), socket_fd);
                        }
                        else if (bytes_read > 0) {
                            // We got something hyhyhy.
                            DATA* dt = (DATA*)recv_data;
                            if (dt->pkt_type_id == DATA_TYPE && dt->pkt_nr == pck_number &&
                                dt->session_id == connection_data.session_id) {
                                // We got our data package :))))))
                                break;        
                            }
                            else if (retransmits_counter == MAX_RETRANSMITS) {
                                // I'm not checking this in loop because if we make the last retransmit,
                                // we still want to see if it had any positive inpact on us.
                                b_connection_closed = true; 
                                break;
                            }
                            else if (dt->pkt_type_id == DATA_TYPE && dt->pkt_nr == pck_number - 1 &&
                                dt->session_id == connection_data.session_id) {
                                // Previous data, we need to retransmit the prev ACK.
                                ACC acc_retr = {.pkt_nr = pck_number - 1, .pkt_type_id = ACC_TYPE, .session_id = connection_data.session_id};
                                bytes_written = sendto(socket_fd, &acc_retr, sizeof(acc_retr),
                                                            flags, (struct sockaddr*)&client_addr, addr_length);
                                b_connection_closed = assert_sendto(bytes_written, sizeof(acc_retr), socket_fd);
                                ++retransmits_counter;
                            }
                            else if (dt->pkt_type_id == CONN_TYPE && dt->session_id != connection_data.session_id) {
                                // Someone wants to connect with us (UwU UwU). REJECT THEM.
                                CONRJT conrjt_pck = {.pkt_type_id = CONRJT_TYPE, .session_id = dt->session_id};
                                bytes_written = sendto(socket_fd, &conrjt_pck, sizeof(conrjt_pck),
                                                            flags, (struct sockaddr*)&client_addr, addr_length);
                                b_connection_closed = assert_sendto(bytes_written, sizeof(conrjt_pck), socket_fd);
                            }
                            else if (dt->pkt_type_id == CONN_TYPE && dt->session_id == connection_data.session_id &&
                                    pck_number == 0) {
                                // We have to retransmit the CONACC package.
                                ssize_t bytes_written = sendto(socket_fd, &resp, sizeof(resp),
                                                            flags, (struct sockaddr*)&client_addr, addr_length);
                                b_connection_closed = assert_sendto(bytes_written, sizeof(resp), socket_fd);
                                ++retransmits_counter;
                            }
                            // Else: some garbage, ignore it.
                        }  
                        else {// errno == EAGAIN
                            if (pck_number == 0) {
                                // Retransmit CONACC.
                                ssize_t bytes_written = sendto(socket_fd, &resp, sizeof(resp),
                                                            flags, (struct sockaddr*)&client_addr, addr_length);
                                b_connection_closed = assert_sendto(bytes_written, sizeof(resp), socket_fd);
                            }
                            else {
                                ACC acc_retr = {.pkt_nr = pck_number - 1, .pkt_type_id = ACC_TYPE, .session_id = connection_data.session_id};
                                bytes_written = sendto(socket_fd, &acc_retr, sizeof(acc_retr),
                                                            flags, (struct sockaddr*)&client_addr, addr_length);
                                b_connection_closed = assert_sendto(bytes_written, sizeof(acc_retr), socket_fd);
                            }

                            ++retransmits_counter;
                        }

                        bytes_read = recvfrom(socket_fd, recv_data, pck_size, flags,
                                    (struct sockaddr*)&client_addr, &addr_length);
                    }

                    // Exited the retransmit loop.
                    if (b_connection_closed && retransmits_counter == MAX_RETRANSMITS) {
                        // Excedeed the retransmits limit.
                        error("Failed to receive data because of the timeout");
                    }
                }
                else {
                    printf("Before while\n");
                    while(bytes_read > 0 && !b_connection_closed) {
                        DATA* dt = (DATA*)recv_data;
                        if (dt->pkt_type_id == DATA_TYPE && dt->pkt_nr == pck_number &&
                            dt->session_id == connection_data.session_id) {
                            // Valid data.
                            printf("Valid data\n");
                            break;
                        }
                        else if(dt->pkt_type_id == CONN_TYPE && dt->session_id != connection_data.session_id) {
                            // Someone wants to connect, send CONRJT.
                            CONRJT conrjt_pck = {.pkt_type_id = CONRJT_TYPE, .session_id = dt->session_id};
                            bytes_written = sendto(socket_fd, &conrjt_pck, sizeof(conrjt_pck),
                                                                flags, (struct sockaddr*)&client_addr, addr_length);
                            b_connection_closed = assert_sendto(bytes_written, sizeof(conrjt_pck), socket_fd);
                        }
                        else {
                            // Garbage packages, get the next package from the buffer.
                            bytes_read = recvfrom(socket_fd, recv_data, pck_size, flags,
                                    (struct sockaddr*)&client_addr, &addr_length);
                        }
                    }
                    
                    printf("After while\n");
                    b_connection_closed = assert_recvfrom(bytes_read, pck_size, socket_fd);
                }

                if (!b_connection_closed) {
                    // We finally managed to get the motherfucking package.
                    DATA* dt = (DATA*)recv_data;
                    byte_count -= dt->data_size;
                    ++pck_number;
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