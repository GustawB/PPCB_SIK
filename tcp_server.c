#include "tcp_server.h"
#include "protconst.h"

#include <signal.h>

bool volatile b_was_tcp_server_interrupted = false;

void tcp_server_handler() {
    b_was_tcp_server_interrupted = true;
}

void run_tcp_server(uint16_t port) {
    // Ignore SIGPIPE signals.
    signal(SIGPIPE, SIG_IGN);
    ignore_signal(tcp_server_handler, SIGINT);

    // Create a socket with IPv4 protocol.
    struct sockaddr_in server_addr;
    int socket_fd = setup_socket(&server_addr, TCP_PROT_ID, port, NULL);

    // Set the socket to listen.
    if(listen(socket_fd, QUEUE_LENGTH) < 0) {
        assert_socket_close(socket_fd);
        syserr("Socket failed to switch to the listening state.");
    }

    // Communication loop:
    while (!b_was_tcp_server_interrupted) {
        struct sockaddr_in client_addr;

        // Accept a connection with a client.
        // Below I'm making a compound literal.
        bool b_connection_closed = false;
        int client_fd = accept(socket_fd, (struct sockaddr*)&client_addr, 
                                &((socklen_t){sizeof(client_addr)}));
        if (client_fd < 0) {
            b_connection_closed = true;
            error("Failed to connect with a client");
        }

        CONN connect_data;
        ssize_t bytes_read = -1;
        struct timeval start, end;
        gettimeofday(&start, NULL);
        long long int send_data = 0;
        printf("Start\n");
        if (!b_was_tcp_server_interrupted && !b_connection_closed) {
            // Set timeouts for the client.
            set_timeouts(socket_fd, client_fd, NULL);

            // Get a CONN package.
            bytes_read = read_n_bytes(client_fd, &connect_data, 
                                                sizeof(connect_data));
            b_connection_closed = assert_read(bytes_read, 
                                                    sizeof(connect_data), 
                                                    socket_fd, client_fd,
                                                    NULL, NULL);
        }

        if (!b_connection_closed && (connect_data.pkt_type_id != CONN_TYPE || 
            connect_data.prot_id != TCP_PROT_ID)) {
            // We got something wrong. Close the connection.
            error("Wanted CONN TCP, got something else");
            b_connection_closed = true;
            assert_socket_close(client_fd);
        }
        if(!b_connection_closed && !b_was_tcp_server_interrupted) {
            // Managed to get the CONN package, its time to send 
            // CONACC back to the client.
            CONACC con_ack_data = {.pkt_type_id = CONACC_TYPE, 
                                    .session_id = connect_data.session_id};
            ssize_t bytes_written = write_n_bytes(client_fd, &con_ack_data, 
                                                    sizeof(con_ack_data));
            b_connection_closed = assert_write(bytes_written, 
                                                sizeof(con_ack_data), 
                                                socket_fd, client_fd, 
                                                NULL, NULL);

            send_data += bytes_written;
            // Read data from the client.
            uint64_t byte_count = be64toh(connect_data.data_length);
            uint64_t pck_number = 0;
            while (byte_count > 0 && !b_connection_closed && !b_was_tcp_server_interrupted) {
                size_t pck_size = sizeof(DATA);
                char* recv_data = malloc(pck_size);
                assert_null(recv_data, socket_fd, client_fd, NULL, NULL);

                bytes_read = read_n_bytes(client_fd, recv_data, 
                                            sizeof(DATA) - 8);
                b_connection_closed = assert_read(bytes_read, 
                                                    sizeof(DATA) - 8, 
                                                    socket_fd, client_fd, 
                                                    recv_data, NULL);
                if (!b_connection_closed) {
                    DATA* dt = (DATA*)recv_data;
                    if (dt->pkt_type_id != DATA_TYPE || 
                        dt->session_id != connect_data.session_id || 
                        be64toh(dt->pkt_nr) != pck_number || 
                        !assert_data_size(be32toh(dt->data_size))) {
                        // Invalid package, send RJT to
                        // the client and move on.
                        RJT error_pck = {.session_id = 
                                        connect_data.session_id,
                                        .pkt_type_id = RJT_TYPE, 
                                        .pkt_nr = dt->pkt_nr};
                        bytes_written = write_n_bytes(client_fd, 
                                            &error_pck, sizeof(error_pck));
                        b_connection_closed = assert_write(bytes_written,
                                                sizeof(error_pck), socket_fd,
                                                client_fd, recv_data, NULL);
                        if (!b_connection_closed) {
                            send_data += bytes_written;
                            free(recv_data);
                        }
                        b_connection_closed = true;
                        assert_socket_close(client_fd);
                    }
                    else  {
                        // Valid package, read the data part.
                        char* data_to_print = malloc(be32toh(dt->data_size));
                        assert_null(recv_data, socket_fd, client_fd, 
                                        recv_data, NULL);
                        bytes_read = read_n_bytes(client_fd, data_to_print,
                                                    be32toh(dt->data_size));
                        b_connection_closed = assert_read(bytes_read, 
                                                            be32toh(dt->data_size),
                                                            socket_fd, 
                                                            client_fd, 
                                                            recv_data, 
                                                            data_to_print);
                        if (!b_connection_closed) {
                            // Managed to get the data. Print it.
                            ++pck_number;
                            byte_count -= be32toh(dt->data_size);
                            free(recv_data);
                        }
                        free(data_to_print);
                    }
                }
            }
            
            if (!b_connection_closed && !b_was_tcp_server_interrupted) {
                // Managed to get all the data. Send RCVD package
                // to the client and close the connection.
                RCVD recv_data_ack = {.pkt_type_id = RCVD_TYPE, 
                                        .session_id = connect_data.session_id};
                bytes_written = write_n_bytes(client_fd, &recv_data_ack, 
                                                sizeof(recv_data_ack));
                b_connection_closed = assert_write
                                        (bytes_written, sizeof(recv_data_ack), 
                                        socket_fd, client_fd, NULL, NULL);
            }

            if (!b_connection_closed) {
                // Close the connection.
                send_data += bytes_written;
                assert_socket_close(client_fd);
            }
        }

        if (DEBUG_STATE == 1) {
            gettimeofday(&end, NULL);
            double time_taken = (end.tv_sec - start.tv_sec) * 1e6;
            time_taken = (time_taken + (end.tv_usec - 
                                    start.tv_usec)) * 1e-6;
            printf("\nElapsed: %f seconds\n", time_taken);
            printf("Bytes send in total: %lld\n", send_data);
        }
    }

    assert_socket_close(socket_fd);
}