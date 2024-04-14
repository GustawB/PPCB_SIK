#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <endian.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "tcp_server.h"
#include "protconst.h"
#include "common.h"
#include "err.h"

void run_tcp_server(uint16_t port) {
    // Ignore SIGPIPE signals; stays for now.
    signal(SIGPIPE, SIG_IGN);

    // Create a socket with IPv4 protocol.
    struct sockaddr_in server_addr;
    int socket_fd = setup_socket(&server_addr, TCP_PROT_ID, port, NULL);

    // Set the socket to listen.
    if(listen(socket_fd, QUEUE_LENGTH) < 0) {
        close(socket_fd);
        syserr("ERROR: Socket failed to switch to the listening state.");
    }

    // Communication loop:
    for (;;) {
        struct sockaddr_in client_addr;

        // Accept a connection with a client.
        // Below I'm making a compound literal.
        int client_fd = accept(socket_fd, (struct sockaddr*)&client_addr, 
                                &((socklen_t){sizeof(client_addr)}));
        if (client_fd < 0) {
            close(socket_fd);
            syserr("Failed to connect with a client");
        }

        // Set timeouts for the client.
        set_timeouts(socket_fd, client_fd, NULL);

        struct timeval start, end;

        gettimeofday(&start, NULL);
        long long int send_data = 0;
        printf("Start\n");

        // Get a CONN package.
        CONN connect_data;
        ssize_t bytes_read = read_n_bytes(client_fd, &connect_data, sizeof(connect_data));
        bool b_connection_closed = assert_read(bytes_read, sizeof(connect_data), socket_fd, client_fd, NULL, NULL);
        if (!b_connection_closed && (connect_data.pkt_type_id != CONN_TYPE || 
            connect_data.prot_id != TCP_PROT_ID)) {
            // We got something wrong. Close the connection.
            error("Wanted CONN TCP, got something else");
            b_connection_closed = true;
            close(client_fd);
        }
        if(!b_connection_closed) {
            //printf("Got CONN\n");
            // Managed to get the CONN package, its time to send CONACC back to the client.
            CONACC con_ack_data = {.pkt_type_id = CONACC_TYPE, .session_id = connect_data.session_id};
            ssize_t bytes_written = write_n_bytes(client_fd, &con_ack_data, sizeof(con_ack_data));
            b_connection_closed = assert_write(bytes_written, sizeof(con_ack_data), socket_fd, client_fd, NULL, NULL);
            if (!b_connection_closed) {
                send_data += bytes_written;
                // Read data from the client.
                uint64_t byte_count = be64toh(connect_data.data_length);
                uint64_t pck_number = 0;

                //printf("Send CONACC, byte count: %ld\n", byte_count);
                while (byte_count > 0 && !b_connection_closed) {
                    //printf("Entered read loop\n");

                    size_t pck_size = sizeof(DATA);
                    char* recv_data = malloc(pck_size);
                    assert_malloc(recv_data, socket_fd, client_fd, NULL, NULL);

                    //printf("Current length: %d\n", curr_len);

                    //printf("Data left: %ld\n", byte_count);
                    bytes_read = read_n_bytes(client_fd, recv_data, sizeof(DATA) - 8);
                    //printf("Data read: %ld\n", bytes_read);
                    b_connection_closed = assert_read(bytes_read, sizeof(DATA) - 8, socket_fd, client_fd, recv_data, NULL);
                    //printf("Data asserted\n");
                    if (!b_connection_closed) {
                        //printf("Entered check\n");
                        DATA* dt = (DATA*)recv_data;
                        //printf("Cast succeded\n");
                        //printf("%d %ld ", dt->data_size, byte_count);
                        if (dt->pkt_type_id != DATA_TYPE || dt->session_id != connect_data.session_id || 
                            dt->pkt_nr != pck_number) {
                            // Invalid package, send RJT to the client and move on.
                            RJT error_pck = {.session_id = connect_data.session_id,
                                 .pkt_type_id = RJT_TYPE, .pkt_nr = pck_number};
                            bytes_written = write_n_bytes(client_fd, &error_pck, sizeof(error_pck));
                            b_connection_closed = assert_write(bytes_written, sizeof(error_pck), 
                                                                socket_fd, client_fd, recv_data, NULL);
                            if (!b_connection_closed) {
                                send_data += bytes_written;
                                free(recv_data);
                            }
                            b_connection_closed = true;
                            close(client_fd);
                        }
                        else  {
                            // Valid package, read the data part.
                            char* data_to_print = malloc(dt->data_size + 1);
                            assert_malloc(recv_data, socket_fd, client_fd, recv_data, NULL);
                            bytes_read = read_n_bytes(client_fd, data_to_print, dt->data_size);
                            b_connection_closed = assert_read(bytes_read, dt->data_size, socket_fd, client_fd, recv_data, data_to_print);
                            if (!b_connection_closed) {
                                // Managed to get the data. Print it.
                                //fwrite(data_to_print, sizeof(char), dt->data_size, stdout);
                                //fflush(stdout);
                                ++pck_number;
                                byte_count -= dt->data_size;
                                free(recv_data);
                            }
                            free(data_to_print);
                        }
                    }
                }

                if (!b_connection_closed) {
                    // Managed to get all the data. Send RCVD package 
                    // to the client and close the connection.
                    //printf("Bazinga\n");
                    RCVD recv_data_ack = {.pkt_type_id = RCVD_TYPE, .session_id = connect_data.session_id};
                    bytes_written = write_n_bytes(client_fd, &recv_data_ack, sizeof(recv_data_ack));
                    b_connection_closed = assert_write(bytes_written, sizeof(recv_data_ack), socket_fd, client_fd, NULL, NULL);
                }

                if (!b_connection_closed) {
                    // Close the connection.
                    send_data += bytes_written;
                    close(client_fd);
                }
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
    
    close(socket_fd);
}