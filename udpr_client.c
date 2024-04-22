#include "udpr_client.h"
#include "protconst.h"

bool volatile b_was_udpr_cl_interrupted = false;

void udpr_cl_handler() {
    b_was_udpr_cl_interrupted = true;
}

void run_udpr_client(const struct sockaddr_in* server_addr, char* data, 
                    uint64_t data_length, uint64_t session_id) {
    // Create a socket.
    // Using server_addr directly caused problems, so I'm performing
    // a local copy of the sockaddr_in structure.
    struct sockaddr_in loc_server_addr = *server_addr;
    int socket_fd = create_socket(UDPR_PROT_ID, data);
    ignore_signal(udpr_cl_handler, SIGINT);

    struct timeval start, end;
    long long int send_data = 0;
    gettimeofday(&start, NULL);
    //printf("Start: Session ID: %ld\n", session_id);

    // Set timeouts for the server.
    set_timeouts(-1, socket_fd, data);

    // CONN-CONACK loop
    int retransmit_iter = -1;
    bool b_connection_closed = false;
    while (!b_connection_closed && retransmit_iter < MAX_RETRANSMITS &&
            !b_was_udpr_cl_interrupted) {
        socklen_t addr_length = (socklen_t)sizeof(*server_addr);
        CONN connection_data = {.pkt_type_id = CONN_TYPE, 
                                .session_id = session_id,
                                .prot_id = UDPR_PROT_ID, 
                                .data_length = htobe64(data_length)};
        ssize_t bytes_written = sendto(socket_fd, &connection_data, 
                                        sizeof(connection_data), 0,
                                        (struct sockaddr*)&loc_server_addr,
                                        addr_length);
        b_connection_closed = assert_write(bytes_written, 
                                            sizeof(connection_data), socket_fd,
                                            -1, NULL, data);
        if (!b_connection_closed && !b_was_udpr_cl_interrupted) {
            // Try to get a CONACC package.
            send_data += bytes_written;
            CONACC conacc_pck;
            ssize_t bytes_read = recvfrom(socket_fd, &conacc_pck,
                                    sizeof(conacc_pck), 0,
                                    (struct sockaddr*)&loc_server_addr,
                                    &addr_length);
            if (bytes_read >= 0 || (bytes_read < 0 && errno != EAGAIN)) {
                b_connection_closed = assert_read(bytes_read, 
                                                    sizeof(conacc_pck), 
                                                    socket_fd, -1, NULL, data);
                if (!b_connection_closed) {
                    b_connection_closed = get_connac_pck(&conacc_pck, 
                                                        session_id);
                    if (!b_connection_closed) {
                        // We got CONACC, exit the loop.
                        break;
                    }
                }
            }
            errno = 0;// EAGAIN, repeat the process.
        }

        ++retransmit_iter;
    }

    if(retransmit_iter == MAX_RETRANSMITS) {
        error("Timeout");
        b_connection_closed = true;
    }
    errno = 0; // Clear it from EAGAIN for future purposes.

    // Connection established. Start data sending loop.
    uint64_t pck_number = 0;
    const char* data_ptr = data;
    while(data_length > 0 && !b_connection_closed && !b_was_udpr_cl_interrupted) {
        // recvfrom can change the value of the addr_length,
        // so I have to update it here over and over again.
        socklen_t addr_length = (socklen_t)sizeof(loc_server_addr);
        uint32_t curr_len = calc_pck_size(data_length);

        // Initialize a package.
        ssize_t pck_size = sizeof(DATA) - sizeof(char*) + curr_len;
        char* data_pck = malloc(pck_size);
        assert_null(data_pck, socket_fd, -1, NULL, data);
        
        printf("%ld %d\n", htobe64(pck_number), htobe32(curr_len));
        init_data_pck(session_id, htobe64(pck_number), 
                                htobe32(curr_len), data_pck, data_ptr);

        // Send data to the server.
        ssize_t bytes_written = sendto(socket_fd, data_pck, pck_size, 0,
                                        (struct sockaddr*)&loc_server_addr, 
                                        addr_length);
        b_connection_closed = assert_write(bytes_written, pck_size, socket_fd,
                                            -1, data_pck, data);

        send_data += bytes_written;
        // Managed to send the data, try to get an ACC.
        ACC acc_pck;
        retransmit_iter = 0;
        // DATA-ACC loop.
        while (!b_connection_closed && !b_was_udpr_cl_interrupted) {
            ssize_t bytes_read = recvfrom(socket_fd, &acc_pck,
                                            sizeof(acc_pck), 0,
                                            (struct sockaddr*)&loc_server_addr,
                                            &addr_length);
            if ((bytes_read < 0 && errno != EAGAIN) || bytes_read == 0) { 
                // Will produce error message.
                b_connection_closed = assert_read(bytes_read, sizeof(acc_pck),
                                                socket_fd, -1, data_pck, data);
            }
            else if (bytes_read > 0) {
                if (bytes_read == sizeof(ACC) && acc_pck.pkt_type_id == 
                    ACC_TYPE && acc_pck.session_id == session_id && 
                    be64toh(acc_pck.pkt_nr) == pck_number) {
                    // We received a confirmation, let's proceed.
                    break;
                }
                else if (bytes_read == sizeof(RJT) && acc_pck.pkt_type_id ==
                        RJT_TYPE && acc_pck.session_id == session_id &&
                        be64toh(acc_pck.pkt_nr) == pck_number) {
                    // Valid package got rejected.
                    b_connection_closed = true;
                    error("Data rejected");
                }
                else if (!(bytes_read == sizeof(ACC) && 
                        acc_pck.pkt_type_id == ACC_TYPE && 
                        acc_pck.session_id == session_id && 
                        be64toh(acc_pck.pkt_nr) < pck_number) &&
                        !(bytes_read == sizeof(CONACC) && 
                        acc_pck.pkt_type_id == CONACC_TYPE &&
                        acc_pck.session_id == session_id)) {
                    // Garbage we can't ignore.
                    b_connection_closed = true;
                    error("Invalid package in ACC");
                }
            }
            else { // errno == EAGAIN
                // Connection timeout. Retransmit the data.
                if (retransmit_iter >= MAX_RETRANSMITS) {
                    // Or not because we reached the retransmit limit.
                    b_connection_closed = true;
                    free(data_pck);
                    error("Timeout");
                }
                else {
                    errno = 0;
                    bytes_written = sendto(socket_fd, data_pck, pck_size, 0,
                                            (struct sockaddr*)&loc_server_addr,
                                            addr_length);
                    b_connection_closed = assert_write(bytes_written, pck_size,
                                            socket_fd, -1, data_pck, data);
                    send_data += bytes_written;
                    ++retransmit_iter;
                }
            }
        }
        errno = 0;
        
        if (!b_connection_closed)  {
            // Update invariants after the data-acc loop.
            ++pck_number;
            data_length -= curr_len;
            data_ptr += curr_len;
            free(data_pck);
        }
    }

    if (!b_connection_closed && !b_was_udpr_cl_interrupted) {
        // Get the RCVD package if we managed to send everything.
        RCVD rcvd_pck;
        while (!b_connection_closed) {
            socklen_t addr_length = (socklen_t)sizeof(loc_server_addr);
            ssize_t bytes_read = recvfrom(socket_fd, &rcvd_pck,
                                        sizeof(rcvd_pck), 0,
                                        (struct sockaddr*)&loc_server_addr,
                                        &addr_length);
            if (bytes_read <= 0) { // Will produce error message.
                b_connection_closed = assert_read(bytes_read, sizeof(rcvd_pck),
                                                    socket_fd, -1, NULL, data);
            }
            else if (bytes_read == sizeof(rcvd_pck) && 
                    rcvd_pck.pkt_type_id == RCVD_TYPE && 
                    rcvd_pck.session_id == session_id) {
                // We received a confirmation, exit the loop.
                b_connection_closed = true;
            }
            else if (bytes_read != sizeof(ACC) ||
                    rcvd_pck.session_id != session_id || 
                    (rcvd_pck.pkt_type_id != CONACC_TYPE && 
                    rcvd_pck.pkt_type_id != ACC_TYPE)) {
                // We received something that we can't skip.
                b_connection_closed = true;
                error("Invalid package in RCVD");
            }
        }
    }

    if (DEBUG_STATE == 1) {
        gettimeofday(&end, NULL);
        double time_taken = (end.tv_sec - start.tv_sec) * 1e6;
        time_taken = (time_taken + (end.tv_usec - 
                                start.tv_usec)) * 1e-6;
        //printf("\nElapsed: %f seconds\n", time_taken);
        //printf("Bytes send in total: %lld\n", send_data);
    }

    // End the connection.
    assert_socket_close(socket_fd);
}