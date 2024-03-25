#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <endian.h>
#include <signal.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "TCP_Server.h"
#include "protconst.h"
#include "common.h"
#include "err.h"

void run_tcp_client(struct sockaddr_in* server_addr) {
    printf("TCP client started operating!!!\n");

    // Create a socket with IPv4 protocol.
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(socket_fd < 0){
        syserr("ERROR: Failed to create a socket.");
    }

    printf("%d\n", server_addr->sin_addr.s_addr);
    // Connect to the server.
    if (connect(socket_fd, (struct sockaddr*)server_addr,
                (socklen_t) sizeof(*server_addr)) < 0) {
        syserr("Client failed to connect to the server");
    }

    printf("%d\n", server_addr->sin_addr.s_addr);

    bool bWasAbleToEstablishConnection = true;

    // Send a CONN package to mark the beginning of the connection.
    CONN connect_data = {.pkt_type_id = 1, .session_id = 0, .prot_id = 1, .data_length = 2137};
    ssize_t bytes_written = write_n_bytes(socket_fd, &connect_data, sizeof(connect_data));
    if ((size_t) bytes_written < sizeof(connect_data)) {
        error("Client failed to send the CON package to the server.");
        bWasAbleToEstablishConnection = false;
    }

    // Read a CONACC package but only if we managed to send the CONN package.
    if (bWasAbleToEstablishConnection) {
        CONACC con_ack_data;
        ssize_t bytes_read = read_n_bytes(socket_fd, &con_ack_data, sizeof(con_ack_data));
        if (bytes_read < 0) { // Some kind of error occured.
            if (errno == EAGAIN) {
                error("Connection timeout; finishing execution...");
                bWasAbleToEstablishConnection = false;
            }
            else{
                error("read() in read_n_bytes() on the client side failed.");
                bWasAbleToEstablishConnection = false;
            }

        }
        else if ((size_t)bytes_read < sizeof(con_ack_data)) {
            error("Server failed to send the CONACC package; finishing execution...");
            bWasAbleToEstablishConnection = false;
        }
    }

    // If w managed to both send CONN and receive CONACK, we can proceed
    // to the data transfer.
    if (bWasAbleToEstablishConnection) {
        printf("asdfghjkl\n");
    }
    close(socket_fd);
}