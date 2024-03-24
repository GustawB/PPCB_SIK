#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <endian.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "TCP_Server.h"
#include "protconst.h"
#include "common.h"
#include "err.h"

void run_tcp_server(uint16_t port) {
    printf("Running TCP server!!!\n");

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
        syserr("ERROR: Failed to bind a socket");
    }

    // Set the socket to listen.
    if(listen(socket_fd, QUEUE_LENGTH) < 0) {
        syserr("ERROR: Socket failed to switch to the listening state.");
    }

    printf("Server is listening on port %d...\n", port);

    // Communication loop:
    for (;;) {
        struct sockaddr_in client_addr;

        // Accept a connection with a client.
        // Below I'm making a compound literal.
        int client_fd = accept(socket_fd, (struct sockaddr*)&client_addr, 
                                &((socklen_t){sizeof(client_addr)}));
        if (client_fd < 0) {
            syserr("Failed to connect with a client");
        }

        // Get the IP address of the client (convert it from binary to string).
        const char* client_ip = inet_ntoa(client_addr.sin_addr);
        uint16_t client_port = ntohs(client_addr.sin_port);
        printf("Connected with a client; IP: %s; Port: %d", client_ip, client_port);

        // Set timeouts for the client.
        struct timeval time_options = {.tv_sec = MAX_WAIT, .tv_usec = 0};
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &time_options, sizeof(time_options));
        setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &time_options, sizeof(time_options));

        // Read a CONN package.
        CONN connect_data;
        ssize_t bytes_read = read_n_bytes(client_fd, &connect_data, sizeof(connect_data));
        if (bytes_read < 0) { // Some kind of error occured.
            if (errno == EAGAIN) {
                error("Connection timeout; moving to the next client.");
                continue;
            }
            else{
                error("read() in read_n_bytes() failed.");
                break;
            }

        }
        else if (bytes_read == 0) {
            // Connection closed. Continue the loop.
            continue;
        }
        else if ((size_t)bytes_read < sizeof(connect_data)) {
            error("Client failed to send the CONN package. Moving to the next one...");
            continue;
        }

        // Managed to get CONN package, it's time to send CONACC back to the client.
        CONACC con_ack_data = {.pkt_type_id = 2, .session_id = connect_data.session_id};
        ssize_t bytes_written = write_n_bytes(client_fd, &con_ack_data, sizeof(con_ack_data));
        close(client_fd);
    }
    
    close(socket_fd);
    return 0;
}