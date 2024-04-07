#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <endian.h>
#include <inttypes.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "udp_server.h"
#include "protconst.h"
#include "common.h"
#include "err.h"

void run_udp_server(uint16_t port) {
    // Ignore SIGPIPE signals; stays for now.
    signal(SIGPIPE, SIG_IGN);

    // Create a socket with IPv4 protocol.
    struct sockaddr_in server_addr;
    int socket_fd = setup_socket(&server_addr, UDP_PROT_ID, port);

    // Set timeouts for the client.
    set_timeouts(-1, socket_fd);

    // Communication loop
    for (;;) {
        // Get a CONN package.
        struct sockaddr_in client_addr;
        socklen_t addr_length = (socklen_t)sizeof(client_addr);
        CONN connection_data;
        bool b_connection_closed = false;
        while(!b_connection_closed) {
            ssize_t bytes_read = recvfrom(socket_fd, &connection_data,
                                        sizeof(connection_data), 0,
                                        (struct sockaddr*)&client_addr,
                                        &addr_length);
            
            if ((bytes_read < 0 && errno != EAGAIN) || bytes_read >= 0) {
                b_connection_closed = assert_read(bytes_read, sizeof(connection_data), socket_fd, -1, NULL);
                if (!b_connection_closed && connection_data.pkt_type_id == CONN_TYPE &&
                    (connection_data.prot_id == UDP_PROT_ID || connection_data.prot_id == UDPR_PROT_ID)) {
                    // We got a valid CONN.
                    break;
                }
                else if (bytes_read > 0) {
                    error("Wanted CONN UDP/UDPR, got something else");
                    b_connection_closed = false;
                }
            }
            else {
                // Timeout, reset the error flag.
                errno = 0;
            }
        }        

        // Send CONACC back to the client.
        CONACC resp = {.pkt_type_id = CONACC_TYPE, .session_id = connection_data.session_id};
        ssize_t bytes_written = sendto(socket_fd, &resp, sizeof(resp),
                                    0, (struct sockaddr*)&client_addr, addr_length);
        b_connection_closed = assert_write(bytes_written, sizeof(resp), socket_fd, -1, NULL);

        if(!b_connection_closed) {
            // If we managed to send the CONACC, read the data.
            uint64_t pck_number = 0;
            uint64_t byte_count = be64toh(connection_data.data_length);
            while(byte_count > 0 && !b_connection_closed) {
                addr_length = (socklen_t)sizeof(client_addr);
                uint32_t curr_len = calc_pck_size(byte_count);

                ssize_t pck_size = sizeof(DATA) - 8 + curr_len;
                char* recv_data = malloc(pck_size);
                assert_malloc(recv_data, socket_fd, -1, NULL);
                
                ssize_t bytes_read = recvfrom(socket_fd, recv_data, pck_size, 0,
                                    (struct sockaddr*)&client_addr, &addr_length);
                    int retransmits_counter = 1;
                // Try to get the data.
                while(!b_connection_closed) {
                    printf("Retransmit %d %d\n", retransmits_counter, MAX_RETRANSMITS);
                    if ((bytes_read < 0 && errno != EAGAIN) || bytes_read == 0) { // Will produce error message
                        b_connection_closed = assert_read(bytes_read, pck_size, socket_fd, -1, recv_data);
                    }
                    else if (bytes_read > 0) {
                        // And I can process it further
                        // We got something hyhyhy.
                        DATA* dt = (DATA*)recv_data;
                        if (bytes_read == pck_size && dt->pkt_type_id == DATA_TYPE && 
                            dt->pkt_nr == pck_number && dt->session_id == connection_data.session_id) {
                            // We got our data package :))))))
                            break;        
                        }
                        else if (connection_data.prot_id == UDPR_PROT_ID && retransmits_counter == MAX_RETRANSMITS) {
                            // I'm not checking this in loop because if we make the last retransmit,
                            // we still want to see if it had any positive inpact on us.
                            b_connection_closed = true;
                            error("Failed to receive data because of the timeout");
                        }
                        else if (bytes_read == sizeof(CONN) && dt->pkt_type_id == CONN_TYPE &&
                                    dt->session_id != connection_data.session_id) {
                            // Someone wants to connect with us (UwU UwU). REJECT THEM.
                            CONRJT conrjt_pck = {.pkt_type_id = CONRJT_TYPE, .session_id = dt->session_id};
                            bytes_written = sendto(socket_fd, &conrjt_pck, sizeof(conrjt_pck),
                                                        0, (struct sockaddr*)&client_addr, addr_length);
                            b_connection_closed = assert_write(bytes_written, sizeof(conrjt_pck), socket_fd, -1, recv_data);
                        }
                        else if (connection_data.prot_id == UDPR_PROT_ID && bytes_read == sizeof(CONN) && 
                                dt->pkt_type_id == CONN_TYPE && dt->session_id == connection_data.session_id) {
                            continue;
                        }
                        else if ((size_t)bytes_read >= sizeof(DATA) - 8 &&  dt->pkt_type_id == DATA_TYPE) {
                            if (connection_data.prot_id != UDPR_PROT_ID || dt->pkt_nr >= pck_number || 
                                dt->session_id != connection_data.session_id) {
                                // Someone send us an invalid package. Send him RJT and close the connection if it was our client.
                                RJT rjt_pck = {.pkt_type_id = RJT_TYPE, .session_id = dt->session_id, .pkt_nr = dt->pkt_nr};
                                bytes_written = sendto(socket_fd, &rjt_pck, sizeof(rjt_pck),
                                                                    0, (struct sockaddr*)&client_addr, addr_length);
                                b_connection_closed = assert_write(bytes_written, sizeof(rjt_pck), socket_fd, -1, recv_data);
                                if (dt->session_id == connection_data.session_id) {
                                    // It was our client, we have to close the connection.
                                    b_connection_closed = true;
                                }
                            }
                        }
                        else {
                            // Garbage
                            b_connection_closed = true;
                            error("Invalid package");
                        }
                    }  
                    else {// errno == EAGAIN
                        if (connection_data.prot_id != UDPR_PROT_ID) {
                            // Will produce error message
                            b_connection_closed = assert_read(bytes_read, pck_size, socket_fd, -1, recv_data);
                        }
                        else if (retransmits_counter == MAX_RETRANSMITS) {
                            // Fuck this shit I'm out.
                            b_connection_closed = true;
                            error("Failed to receive data because of the timeout");
                        }
                        else if (pck_number == 0) {
                            // First package, retransmit CONACC.
                            ssize_t bytes_written = sendto(socket_fd, &resp, sizeof(resp),
                                                        0, (struct sockaddr*)&client_addr, addr_length);
                            b_connection_closed = assert_write(bytes_written, sizeof(resp), socket_fd, -1, recv_data);
                            ++retransmits_counter;
                        }
                        else {
                            // Retransmit ACC.
                            ACC acc_retr = {.pkt_nr = pck_number - 1, .pkt_type_id = ACC_TYPE, .session_id = connection_data.session_id};
                            bytes_written = sendto(socket_fd, &acc_retr, sizeof(acc_retr),
                                                        0, (struct sockaddr*)&client_addr, addr_length);
                            b_connection_closed = assert_write(bytes_written, sizeof(acc_retr), socket_fd, -1, recv_data);
                            ++retransmits_counter;
                        }
                    }

                    if (!b_connection_closed) {
                        bytes_read = recvfrom(socket_fd, recv_data, pck_size, 0,
                                (struct sockaddr*)&client_addr, &addr_length);
                    }
                }

                if (!b_connection_closed) {
                    // We finally managed to get the motherfucking package.
                    DATA* dt = (DATA*)recv_data;
                    byte_count -= dt->data_size;
                    ++pck_number;

                    char* data_to_print = malloc(curr_len + 1);
                    assert_malloc(data_to_print, socket_fd, -1, NULL);
                    print_data(recv_data + 21, data_to_print, curr_len);
                    free(recv_data);

                    if (connection_data.prot_id == UDPR_PROT_ID) {
                        // Send the ACK package.
                        ACC acc_resp = {.pkt_type_id = ACC_TYPE, .pkt_nr = pck_number - 1, .session_id = connection_data.session_id};
                        bytes_written = sendto(socket_fd, &acc_resp, sizeof(acc_resp),
                                                    0, (struct sockaddr*)&client_addr, addr_length);
                        b_connection_closed = assert_write(bytes_written, sizeof(acc_resp), socket_fd, -1, NULL);
                    }
                }
            }

            if(!b_connection_closed) {
                // we got all the data, now we immediately send RCVD and end the connection.
                RCVD rcvd_resp = {.pkt_type_id = RCVD_TYPE, .session_id = connection_data.session_id};
                bytes_written = sendto(socket_fd, &rcvd_resp, sizeof(rcvd_resp),
                                                    0, (struct sockaddr*)&client_addr, addr_length);
                b_connection_closed = assert_write(bytes_written, sizeof(rcvd_resp), socket_fd, -1, NULL);
            }
        }
    }

    close(socket_fd);
}