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

    // CONN-CONACK loop
    int flags = 0;
    int retransmit_iter = 0;
    bool b_connecton_closed = false;
    while (retransmit_iter < MAX_RETRANSMITS && !b_connecton_closed) {
        socklen_t addr_length = (socklen_t)sizeof(*server_addr);
        CONN connection_data = {.pkt_type_id = CONN_TYPE, .session_id = session_id,
                                .prot_id = UDP_PROT_ID, .data_length = htobe64(data_length)};
        ssize_t bytes_written = sendto(socket_fd, &connection_data, sizeof(connection_data),
                                        flags, (struct sockaddr*)&loc_server_addr, addr_length);
        b_connecton_closed = assert_sendto(bytes_written, sizeof(connection_data), socket_fd);
        if (!b_connecton_closed) {
            // Try to get a CONACK package.
            CONACC conacc_pck;
            ssize_t bytes_read = recvfrom(socket_fd, &conacc_pck,
                                    sizeof(conacc_pck), flags,
                                    (struct sockaddr*)&server_addr,
                                    &addr_length);
            if (bytes_read >= 0 || (bytes_read < 0 && errno != EAGAIN)) {
                b_connecton_closed = assert_recvfrom(bytes_read, sizeof(conacc_pck), socket_fd);
                if (!b_connecton_closed && conacc_pck.session_id == session_id) {
                    // We got CONACC or RJT FROM OUR SESSION.
                    if (conacc_pck.pkt_type_id != CONACC_TYPE) {
                        // RJT
                        b_connecton_closed = true;
                    }
                    break;
                }
            }
        }

        ++retransmit_iter;
    }

    if (!b_connecton_closed && retransmit_iter < MAX_RETRANSMITS) {
        // Connection establisher. Start data sending loop.
        uint64_t pck_number = 0;
        const char* data_ptr = data;
        bool b_was_rcvd_received = false;
        socklen_t addr_length;
        while(data_length > 0 && !b_connecton_closed) {
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
            init_data_pck(session_id, pck_number, 
                                    curr_len, data_pck, data_ptr);
            
            // Send data to the server.
            ssize_t bytes_written = sendto(socket_fd, data_pck, pck_size, flags,
                                    (struct sockaddr*)&loc_server_addr, addr_length);
            b_connecton_closed = assert_sendto(bytes_written, pck_size, socket_fd);
            
            if (!b_connecton_closed) {
                // Managed to send the data, try to get an ACC.
                ACC acc_pck;
                retransmit_iter = 1;
                // DATA-ACC loop.
                while (retransmit_iter < MAX_RETRANSMITS && !b_connecton_closed) {
                    ssize_t bytes_read = recvfrom(socket_fd, &acc_pck,
                                                    sizeof(acc_pck), flags,
                                                    (struct sockaddr*)&loc_server_addr,
                                                    &addr_length);
                    if ((bytes_read < 0 && errno != EAGAIN) || bytes_read == 0) {
                        b_connecton_closed = assert_recvfrom(bytes_read, sizeof(acc_pck), socket_fd);
                    }
                    else if (bytes_read > 0 && acc_pck.session_id == session_id) {
                        // We read something from our session.
                        if (acc_pck.pkt_type_id == RJT_TYPE) {
                            // Package got rejected, kill yourself.
                            b_connecton_closed = true;
                        }
                        else if (acc_pck.pkt_type_id == ACC_TYPE && acc_pck.pkt_nr == pck_number - 1) {
                            // Server didn't get the data pck, we have to retransmit it.
                            bytes_written = sendto(socket_fd, data_pck, pck_size, flags,
                                            (struct sockaddr*)&loc_server_addr, addr_length);
                            b_connecton_closed = assert_sendto(bytes_written, pck_size, socket_fd);
                            ++retransmit_iter;
                        }
                        else if (acc_pck.pkt_type_id == ACC_TYPE && acc_pck.pkt_nr == pck_number) {
                            // We received a confirmation, let's proceed.
                            break;
                        }
                        else if (data_length - curr_len == 0 && acc_pck.pkt_type_id == RCVD_TYPE) {
                            // We send everything, and ACC got lost BUT we managed to retrieve
                            // the next package (in this case there is no harm in that).
                            b_was_rcvd_received = true;
                            break;
                        }
                        // I guess the idea for now is that I ignore garbage,
                        // Let's return to that later.
                    }
                    else if (bytes_read < 0 && errno == EAGAIN) {
                        // Connection timeout. Retransmit the data.
                        bytes_written = sendto(socket_fd, data_pck, pck_size, flags,
                                        (struct sockaddr*)&loc_server_addr, addr_length);
                        b_connecton_closed = assert_sendto(bytes_written, pck_size, socket_fd);
                        ++retransmit_iter;
                    }
                }   
            }

            // Update invariants after the data-acc loop.
            ++pck_number;
            data_length -= curr_len;
            data_ptr += curr_len;
        }

        if (!b_connecton_closed && retransmit_iter < MAX_RETRANSMITS && !b_was_rcvd_received) {
            // Get the RCVD package if we already didn't do that and if everything is ok.
            RCVD rcvd_pck;
            while (!b_connecton_closed) {
                ssize_t bytes_read = recvfrom(socket_fd, &rcvd_pck,
                                            sizeof(rcvd_pck), flags,
                                            (struct sockaddr*)&loc_server_addr,
                                            &addr_length);
                if (bytes_read <= 0) {
                    b_connecton_closed = assert_recvfrom(bytes_read, sizeof(rcvd_pck), socket_fd);
                }
                else if (rcvd_pck.pkt_type_id == RCVD_TYPE) {
                    break;
                }
            }
        }
    }

    // End the connection.
    close(socket_fd);
}