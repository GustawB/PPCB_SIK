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

#include "tcp_server.h"
#include "protconst.h"
#include "common.h"
#include "err.h"

void run_tcp_server(uint16_t port) {
    // Ignore SIGPIPE signals; stays for now.
    signal(SIGPIPE, SIG_IGN);

    // Create a socket with IPv4 protocol.
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
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
        struct timeval time_options = {.tv_sec = MAX_WAIT, .tv_usec = 0};
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &time_options, sizeof(time_options));
        setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &time_options, sizeof(time_options));

        // Get a CONN package.
        CONN connect_data;
        ssize_t bytes_read = read_n_bytes(client_fd, &connect_data, sizeof(connect_data));
        bool b_connection_closed = assert_read(bytes_read, sizeof(connect_data), socket_fd, client_fd);
        if (connect_data.pkt_type_id != CONN_TYPE || connect_data.prot_id != TCP_PROT_ID) {
            // We got something wrong. Close the connection.
            b_connection_closed = true;
        }
        if(!b_connection_closed) {
            // Managed to get the CONN package, its time to send CONACC back to the client.
            CONACC con_ack_data = {.pkt_type_id = CONACC_TYPE, .session_id = connect_data.session_id};
            ssize_t bytes_written = write_n_bytes(client_fd, &con_ack_data, sizeof(con_ack_data));
            b_connection_closed = assert_write(bytes_written, sizeof(con_ack_data), socket_fd, client_fd);
            if (!b_connection_closed) {
                // Read data from the client.
                uint64_t byte_count = be64toh(connect_data.data_length);
                uint64_t pck_number = 0;

                while(byte_count > 0 && !b_connection_closed) {
                    uint32_t curr_len = PCK_SIZE;
                    if (curr_len > byte_count) {
                        curr_len = byte_count;
                    }

                    size_t pck_size = sizeof(DATA) - 8 + curr_len;
                    char* recv_data = malloc(pck_size);
                    if (recv_data == NULL) {
                        close(socket_fd);
                        close(client_fd);
                        fatal("Malloc in data reading failed.");
                    }

                    bytes_read = read_n_bytes(client_fd, recv_data, pck_size);
                    b_connection_closed = assert_read(bytes_read, pck_size, socket_fd, client_fd);
                    if (!b_connection_closed) {
                        DATA* dt = (DATA*)recv_data;
                        if (dt->pkt_type_id != DATA_TYPE || dt->session_id != connect_data.session_id || 
                            dt->pkt_nr != pck_number || dt->data_size != curr_len) {
                            // Invalid package, send RJT to the client and move on.
                            free(recv_data);
                            RJT error_pck = {.session_id = connect_data.session_id,
                                 .pkt_type_id = RJT_TYPE, .pkt_nr = pck_number};
                            bytes_written = write_n_bytes(client_fd, &error_pck, sizeof(error_pck));
                            b_connection_closed = assert_write(bytes_written, sizeof(error_pck), 
                                                                socket_fd, client_fd);
                        }
                        else  {
                            byte_count -= dt->data_size;
                            printf("Data: %s\n", recv_data + 21);
                            free(recv_data);
                        }
                    }
                    ++pck_number;
                }

                if (!b_connection_closed) {
                    // Managed to get all the data. Send RCVD package 
                    // to the client and close the connection.
                    RCVD recv_data_ack = {.pkt_type_id = RCVD_TYPE, .session_id = connect_data.session_id};
                    bytes_written = write_n_bytes(client_fd, &recv_data_ack, sizeof(recv_data_ack));
                    b_connection_closed = assert_write(bytes_written, sizeof(recv_data_ack), socket_fd, client_fd);
                }

                // Close the connection.
                close(client_fd);
            }
        }
    }
    
    close(socket_fd);
}