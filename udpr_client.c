#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
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

void run_udpr_client(const struct sockaddr_in* server_addr, const char* data, 
                    uint64_t data_length, uint64_t session_id) {
    printf("UDPR CLIENT\n");
    // Create a socket.
    // Using server_addr directly caused problems, so I'm performing
    // a local copy of the sockaddr_in structure.
    struct sockaddr_in loc_server_addr = *server_addr;
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(socket_fd < 0){
        syserr("Failed to create a socket.");
    }

    // Set timeouts for the server.
    struct timeval time_options = {.tv_sec = MAX_WAIT, .tv_usec = 0};
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &time_options, sizeof(time_options));
    setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &time_options, sizeof(time_options));

    // CONN-CONACK loop
    int flags = 0;
    int retransmit_iter = 0;
    bool b_connecton_closed = false;
    while (!b_connecton_closed && retransmit_iter < MAX_RETRANSMITS) {
        socklen_t addr_length = (socklen_t)sizeof(*server_addr);
        CONN connection_data = {.pkt_type_id = CONN_TYPE, .session_id = session_id,
                                .prot_id = UDPR_PROT_ID, .data_length = htobe64(data_length)};
        ssize_t bytes_written = sendto(socket_fd, &connection_data, sizeof(connection_data),
                                        flags, (struct sockaddr*)&loc_server_addr, addr_length);
        b_connecton_closed = assert_write(bytes_written, sizeof(connection_data), socket_fd, -1, NULL);
        if (!b_connecton_closed) {
            // Try to get a CONACC package.
            CONACC conacc_pck;
            ssize_t bytes_read = recvfrom(socket_fd, &conacc_pck,
                                    sizeof(conacc_pck), flags,
                                    (struct sockaddr*)&loc_server_addr,
                                    &addr_length);
            if (bytes_read >= 0 || (bytes_read < 0 && errno != EAGAIN)) {
                b_connecton_closed = assert_read(bytes_read, sizeof(conacc_pck), socket_fd, -1, NULL);
                if (!b_connecton_closed && conacc_pck.pkt_type_id == CONACC_TYPE &&
                    conacc_pck.session_id == session_id) {
                    // We got CONACC, break from the loop.
                    break;
                }
                else if (!b_connecton_closed && conacc_pck.pkt_type_id == CONRJT_TYPE &&
                    conacc_pck.session_id == session_id) {
                    // We got rejected.
                    b_connecton_closed = true;
                    error("Connection rejected");
                }
                else if (!b_connecton_closed) {
                    // We got garbage.
                    b_connecton_closed = true;
                    error("Invalid package");
                }
            }
        }

        ++retransmit_iter;
    }

    if (!b_connecton_closed && retransmit_iter < MAX_RETRANSMITS) {
        // Connection established. Start data sending loop.
        uint64_t pck_number = 0;
        const char* data_ptr = data;
        socklen_t addr_length;
        while(data_length > 0 && !b_connecton_closed) {
            // recvfrom can change the value of the addr_length,
            // so I have to update it here over and over again.
            addr_length = (socklen_t)sizeof(loc_server_addr);
            uint32_t curr_len = PCK_SIZE;
            if (curr_len > data_length) {
                curr_len = data_length;
            }

            // Initialize a package.
            ssize_t pck_size = sizeof(DATA) - 8 + curr_len;
            char* data_pck = malloc(pck_size);
            assert_malloc(data_pck, socket_fd, -1, NULL);
            
            init_data_pck(session_id, pck_number, 
                                    curr_len, data_pck, data_ptr);
            // Send data to the server.
            ssize_t bytes_written = sendto(socket_fd, data_pck, pck_size, flags,
                                    (struct sockaddr*)&loc_server_addr, addr_length);
            b_connecton_closed = assert_write(bytes_written, pck_size, socket_fd, -1, data_pck);
            
            if (!b_connecton_closed) {
                // Managed to send the data, try to get an ACC.
                ACC acc_pck;
                retransmit_iter = 1;
                // DATA-ACC loop.
                while (!b_connecton_closed) {
                    printf("Retransmit %d %d\n", retransmit_iter, MAX_RETRANSMITS);
                    ssize_t bytes_read = recvfrom(socket_fd, &acc_pck,
                                                    sizeof(acc_pck), flags,
                                                    (struct sockaddr*)&loc_server_addr,
                                                    &addr_length);
                    if ((bytes_read < 0 && errno != EAGAIN) || bytes_read == 0) {
                        b_connecton_closed = assert_read(bytes_read, sizeof(acc_pck), socket_fd, -1, data_pck);
                    }
                    else if (bytes_read > 0) {
                        if (bytes_read == sizeof(ACC) && acc_pck.pkt_type_id == ACC_TYPE &&
                            acc_pck.session_id == session_id && acc_pck.pkt_nr == pck_number) {
                            // We received a confirmation, let's proceed.
                            break;
                        }
                        else if (retransmit_iter == MAX_RETRANSMITS) {
                            // We reached the retransmit limit and got nothing, it's time to close the connection.
                            b_connecton_closed = true;
                        }
                        else if (bytes_read == sizeof(RJT) && acc_pck.pkt_type_id == RJT_TYPE && 
                                acc_pck.session_id == session_id) {
                            // Package got rejected, kill yourself.
                            b_connecton_closed = true;
                            close(socket_fd);
                            error("Data rejected");
                        }
                        else if (bytes_read == sizeof(ACC) && acc_pck.pkt_type_id == ACC_TYPE &&
                            acc_pck.session_id == session_id && acc_pck.pkt_nr < pck_number) {
                            continue;
                        }
                        else if (bytes_read == sizeof(CONACC) && acc_pck.pkt_type_id == CONACC_TYPE &&
                            acc_pck.session_id == session_id) {
                            continue;
                        }
                        else {
                            // Garbage we can't ignore.
                            b_connecton_closed = true;
                            error("Invalid package in ACC");
                        }
                    }
                    else { // errno == EAGAIN
                        // Connection timeout. Retransmit the data.
                        if (retransmit_iter >= MAX_RETRANSMITS) {
                            b_connecton_closed = true;
                        }
                        else {
                            bytes_written = sendto(socket_fd, data_pck, pck_size, flags,
                                            (struct sockaddr*)&loc_server_addr, addr_length);
                            b_connecton_closed = assert_write(bytes_written, pck_size, socket_fd, -1, NULL);
                            ++retransmit_iter;
                        }
                    }
                }

                if (b_connecton_closed && retransmit_iter == MAX_RETRANSMITS) {
                    error("Timeout");
                }
            }
            
            if (!b_connecton_closed)  {
                // Update invariants after the data-acc loop.
                ++pck_number;
                data_length -= curr_len;
                data_ptr += curr_len;
                free(data_pck);
            }
        }

        if (!b_connecton_closed) {
            // Get the RCVD package if we managed to send everything.
            RCVD rcvd_pck;
            while (!b_connecton_closed) {
                ssize_t bytes_read = recvfrom(socket_fd, &rcvd_pck,
                                            sizeof(rcvd_pck), flags,
                                            (struct sockaddr*)&loc_server_addr,
                                            &addr_length);
                if (bytes_read <= 0) {
                    b_connecton_closed = assert_read(bytes_read, sizeof(rcvd_pck), socket_fd, -1, NULL);
                }
                else if (rcvd_pck.pkt_type_id == RCVD_TYPE && rcvd_pck.session_id == session_id) {
                    // We received a confirmation, exit the loop.
                    b_connecton_closed = true;
                }
                else if (rcvd_pck.session_id != session_id || (rcvd_pck.pkt_type_id != CONACC_TYPE && 
                rcvd_pck.pkt_type_id != ACC_TYPE)) {
                    // We received something that we can't skip.
                    b_connecton_closed = true;
                    error("Invalid package in RCVD");
                }
            }
        }
    }

    // End the connection.
    close(socket_fd);
}