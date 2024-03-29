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
    server_addr.sin_family = AF_INET; // IPv4 protocol.
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Listening on all interfaces.
    server_addr.sin_port = htons(port);

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

        // Get the IP address of the client (convert it from binary to string).
        const char* client_ip = inet_ntoa(client_addr.sin_addr);
        uint16_t client_port = ntohs(client_addr.sin_port);

        // Set timeouts for the client.
        struct timeval time_options = {.tv_sec = MAX_WAIT, .tv_usec = 0};
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &time_options, sizeof(time_options));
        setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &time_options, sizeof(time_options));

        // Read a CONN package.
        CONN connect_data;
        ssize_t bytes_read = read_n_bytes(client_fd, &connect_data, sizeof(connect_data));
        if (bytes_read == 0) {
            // Connection closed. Continue the loop.
            continue;
        }
        else if (assert_read(bytes_read, sizeof(connect_data))) {
            // Managed to get the CONN package, its time to send CONACC back to the client.
            uint64_t session_id = connect_data.session_id;
            CONACC con_ack_data = {.pkt_type_id = CONACC_TYPE, .session_id = connect_data.session_id};
            ssize_t bytes_written = write_n_bytes(client_fd, &con_ack_data, sizeof(con_ack_data));
            if ((size_t) bytes_written < sizeof(con_ack_data)) {
                error("Server failed to send the CONACC package back to the client.");
            }
            close(client_fd);
            close(socket_fd);
            break;
            // Read data from the client.
            uint64_t byte_count = be64toh(connect_data.data_length);
            bool b_connection_closed = false;
            while(byte_count > 0) {
                uint32_t curr_len = PCK_SIZE;
                if (curr_len > byte_count) {
                    curr_len = byte_count;
                }
                size_t pck_size = sizeof(DATA) - 8 + curr_len;
                char* recv_data = malloc(pck_size);
                bytes_read = read_n_bytes(client_fd, recv_data, pck_size);
                if (bytes_read == 0) {
                    // Client closed a connection, we have to do the same.
                    b_connection_closed = true;
                    break;
                }
                else if (assert_read(bytes_read, sizeof(DATA) - 8 + curr_len)) {
                    assert_read(bytes_read, sizeof(DATA) - 8 + curr_len);
                    DATA* dt = (DATA*)recv_data;
                    byte_count -= dt->data_size;
                    printf("Data: %s\n", recv_data + 21);
                }
            }

            // Managed to get all the data. Send RCVD package to the client and close the connection.
            RCVD recv_data_ack = {.pkt_type_id = 7, .session_id = 2137};
            bytes_written = write_n_bytes(client_fd, &recv_data_ack, sizeof(recv_data_ack));
            if ((size_t) bytes_written < sizeof(recv_data_ack)) {
                error("Server failed to send the RCVD package to the client.");
            }

            printf("Closing a connection with the client...\n");
            close(client_fd);
        }
    }

    //close(socket_fd);
}