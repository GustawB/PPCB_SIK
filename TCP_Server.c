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
    }
}