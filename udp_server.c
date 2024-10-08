#include "udp_server.h"
#include "protconst.h"

#define MAX_PACKET_SIZE 65536

bool volatile b_was_udp_server_interrupted = false;

void udp_server_handler() {
    b_was_udp_server_interrupted = true;
}

void run_udp_server(uint16_t port) {
    // Ignore SIGPIPE signals.
    signal(SIGPIPE, SIG_IGN);
    ignore_signal(udp_server_handler, SIGINT);

    // Buffer for reading datagrams.
    char* recv_data = malloc(MAX_PACKET_SIZE);
    assert_null(recv_data, -1, -1, NULL, NULL);

    // Create a socket with IPv4 protocol.
    struct sockaddr_in server_addr;
    int socket_fd = setup_socket(&server_addr, UDP_PROT_ID, port, recv_data);

    // Set timeouts for the client.
    set_timeouts(-1, socket_fd, NULL);

    // Communication loop
    while(!b_was_udp_server_interrupted) {
        // Get a CONN package.
        struct sockaddr_in client_addr;
        socklen_t addr_length = (socklen_t)sizeof(client_addr);
        CONN connection_data;
        bool b_connection_closed = false;
        while(!b_connection_closed && !b_was_udp_server_interrupted) {
            ssize_t bytes_read = recvfrom(socket_fd, &connection_data,
                                        sizeof(connection_data), 0,
                                        (struct sockaddr*)&client_addr,
                                        &addr_length);
            if ((bytes_read < 0 && errno != EAGAIN) || bytes_read >= 0) {
                b_connection_closed = assert_read
                                        (bytes_read, sizeof(connection_data),
                                        socket_fd, -1, NULL, recv_data);
                if (!b_connection_closed && 
                    connection_data.pkt_type_id == CONN_TYPE &&
                    (connection_data.prot_id == UDP_PROT_ID || 
                    connection_data.prot_id == UDPR_PROT_ID)) {
                    // We got a valid CONN.
                    break;
                }
                else if (bytes_read > 0) {
                    //error("Wanted CONN UDP/UDPR, got something else");
                    b_connection_closed = false;
                }
            }
            errno = 0; // EAGAIN, clear the error.
        }

        // Send CONACC back to the client.
        CONACC resp = {.pkt_type_id = CONACC_TYPE, 
                        .session_id = connection_data.session_id};
        ssize_t bytes_written = sendto(socket_fd, &resp, sizeof(resp),
                                    0, (struct sockaddr*)&client_addr, 
                                    addr_length);
        b_connection_closed = assert_write(bytes_written, sizeof(resp),
                                            socket_fd, -1, NULL, recv_data);

        // If we managed to send the CONACC, read the data.
        uint64_t pck_number = 0;
        uint64_t byte_count = be64toh(connection_data.data_length);
        while(byte_count > 0 && !b_connection_closed && !b_was_udp_server_interrupted) {
            addr_length = (socklen_t)sizeof(client_addr);
            ssize_t bytes_read = recvfrom(socket_fd, recv_data, 
                                        MAX_PACKET_SIZE, 0,
                                        (struct sockaddr*)&client_addr, 
                                        &addr_length);
            int retransmits_counter = 0;
            // Try to get the data.
            while(!b_connection_closed && !b_was_udp_server_interrupted) {
                if ((bytes_read < 0 && errno != EAGAIN) || bytes_read == 0) { 
                    // Will produce error message
                    b_connection_closed = assert_read
                                        (bytes_read, MAX_PACKET_SIZE, 
                                        socket_fd, -1, NULL, recv_data);
                }
                else if (bytes_read > 0) {
                    // I can process data further.
                    // We got something.
                    DATA* dt = (DATA*)recv_data;
                    if (bytes_read >= (int32_t)(sizeof(DATA) - sizeof(char*)) && 
                        dt->pkt_type_id == DATA_TYPE && 
                        be64toh(dt->pkt_nr) == pck_number && dt->session_id == 
                        connection_data.session_id && 
                        assert_data_size(be32toh(dt->data_size))) {
                        // We got our data package :))))))
                        break;        
                    }
                    else if ((size_t)bytes_read >= sizeof(DATA) - sizeof(char*) && 
                            dt->pkt_type_id == DATA_TYPE) {
                        if (connection_data.prot_id != UDPR_PROT_ID || 
                            be64toh(dt->pkt_nr) >= pck_number || 
                            dt->session_id != connection_data.session_id ||
                            !assert_data_size(be32toh(dt->data_size))) {
                            // Someone send us an invalid package. Send him 
                            // RJT and close the connection if it was our client.
                            RJT rjt_pck = {.pkt_type_id = RJT_TYPE, 
                                            .session_id = dt->session_id, 
                                            .pkt_nr = dt->pkt_nr};
                            bytes_written = sendto(socket_fd, &rjt_pck, 
                                                sizeof(rjt_pck), 0,
                                                (struct sockaddr*)&client_addr,
                                                addr_length);
                            b_connection_closed = assert_write(bytes_written,
                                                    sizeof(rjt_pck), socket_fd,
                                                    -1, NULL, recv_data);
                            if (dt->session_id == connection_data.session_id) {
                                // It was our client, we have 
                                // to close the connection.
                                b_connection_closed = true;
                            }
                        }
                    }
                    else if (bytes_read == sizeof(CONN) && 
                            dt->pkt_type_id == CONN_TYPE &&
                            dt->session_id != connection_data.session_id) {
                        // Someone wants to connect with us (UwU UwU). 
                        // REJECT THEM.
                        CONRJT conrjt_pck = {.pkt_type_id = CONRJT_TYPE, 
                                            .session_id = dt->session_id};
                        bytes_written = sendto(socket_fd, 
                                            &conrjt_pck, sizeof(conrjt_pck),
                                            0, (struct sockaddr*)&client_addr,
                                            addr_length);
                        b_connection_closed = assert_write(bytes_written, 
                                                            sizeof(conrjt_pck),
                                                            socket_fd, -1, 
                                                            NULL, recv_data);
                    }
                    else if (!(bytes_read == sizeof(CONN) && 
                            connection_data.prot_id == UDPR_PROT_ID && 
                            dt->pkt_type_id == CONN_TYPE && 
                            dt->session_id == connection_data.session_id)) {
                        // Garbage we can't ignore.
                        b_connection_closed = true;
                        error("Invalid package");
                    }
                }  
                else {// errno == EAGAIN
                    if (connection_data.prot_id != UDPR_PROT_ID) {
                        // Will produce error message
                        b_connection_closed = assert_read(bytes_read, 
                                            MAX_PACKET_SIZE, socket_fd,
                                            -1, NULL, recv_data);
                    }
                    else if (retransmits_counter == MAX_RETRANSMITS) {
                        // Reached retransmit limit, exit.
                        b_connection_closed = true;
                        error("Failed to receive data because of the timeout");
                    }
                    else if (pck_number == 0) {
                        errno = 0;
                        // First package, retransmit CONACC.
                        ssize_t bytes_written = 
                        sendto(socket_fd, &resp, sizeof(resp), 0, 
                                (struct sockaddr*)&client_addr, addr_length);
                        b_connection_closed = 
                        assert_write(bytes_written, sizeof(resp), socket_fd,
                                        -1, NULL, recv_data);
                        ++retransmits_counter;
                    }
                    else {
                        errno = 0;
                        // Retransmit ACC.
                        ACC acc_retr = {.pkt_nr = htobe64(pck_number - 1), 
                                    .pkt_type_id = ACC_TYPE, 
                                    .session_id = connection_data.session_id};
                        bytes_written = 
                        sendto(socket_fd, &acc_retr, sizeof(acc_retr), 0, 
                                (struct sockaddr*)&client_addr, addr_length);
                        b_connection_closed = 
                        assert_write(bytes_written, sizeof(acc_retr), 
                                    socket_fd, -1, NULL, recv_data);
                        ++retransmits_counter;
                    }
                }

                if (!b_connection_closed) {
                    bytes_read = 
                    recvfrom(socket_fd, recv_data, MAX_PACKET_SIZE, 0,
                            (struct sockaddr*)&client_addr, &addr_length);
                }
            }

            if (!b_connection_closed && !b_was_udp_server_interrupted) {
                // We finally managed to get the package.
                DATA* dt = (DATA*)recv_data;
                if (byte_count < byte_count - be32toh(dt->data_size)) {
                    byte_count = 0;
                }
                else {
                    byte_count -= be32toh(dt->data_size);
                }
                ++pck_number;

                print_data(recv_data + sizeof(DATA) - sizeof(char*), be32toh(dt->data_size));

                if (connection_data.prot_id == UDPR_PROT_ID) {
                    // Send the ACK package.
                    ACC acc_resp = {.pkt_type_id = ACC_TYPE, 
                                    .pkt_nr = htobe64(pck_number - 1), 
                                    .session_id = connection_data.session_id};
                    bytes_written = 
                        sendto(socket_fd, &acc_resp, sizeof(acc_resp), 0,
                        (struct sockaddr*)&client_addr, addr_length);
                    b_connection_closed = 
                        assert_write(bytes_written, sizeof(acc_resp), 
                        socket_fd, -1, NULL, recv_data);
                }
            }
        }

        if(!b_connection_closed && !b_was_udp_server_interrupted) {
            // We got all the data, now we immediately 
            // send RCVD and end the connection.
            RCVD rcvd_resp = {.pkt_type_id = RCVD_TYPE, 
                                .session_id = connection_data.session_id};
            bytes_written = 
            sendto(socket_fd, &rcvd_resp, sizeof(rcvd_resp), 0, 
                (struct sockaddr*)&client_addr, addr_length);
            b_connection_closed = assert_write(bytes_written, 
                sizeof(rcvd_resp), socket_fd, -1, NULL, recv_data);
        }
    }

    free(recv_data);
    assert_socket_close(socket_fd);
}