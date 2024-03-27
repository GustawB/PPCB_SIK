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

#include "TCP_Server.h"
#include "protconst.h"
#include "common.h"
#include "err.h"

void run_tcp_client(struct sockaddr_in* server_addr, const char* data, uint64_t data_length) {
    printf("TCP client started operating!!!\n");
    printf("Data_1 length: %ld\n", data_length);

    uint64_t session_id = 2137;
    uint8_t protocol_id = 3;

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

    // Send a CONN package to mark the beginning of the connection.
    CONN connect_data = {.pkt_type_id = 1, .session_id = 0, .prot_id = 1, .data_length = htobe64(data_length)};
    ssize_t bytes_written = write_n_bytes(socket_fd, &connect_data, sizeof(connect_data));
    if ((size_t) bytes_written < sizeof(connect_data)) {
        error("Client failed to send a CONN package to the server.");
        close(socket_fd);
        return;
    }

    // Read a CONACC package but only if we managed to send the CONN package.
    CONACC con_ack_data;

    // If w managed to both send CONN and receive CONACK, we can proceed
    // to the data transfer.
    if(assert_read(read_n_bytes(socket_fd, &con_ack_data, sizeof(con_ack_data)), sizeof(con_ack_data))) {
        printf("Sending data...\n");

        uint64_t pck_iter = 0;
        const char* data_ptr = data;
        while(data_length > 0) {
            // Calculate a size of the data chunk that will be sent
            // in the current package.
            uint32_t curr_len = PCK_SIZE;
            if (curr_len > data_length) {
                curr_len = data_length;
            }

            // Initialize a package.
            int size = sizeof(DATA) - 1 + data_length;
            printf("Size: %d\n", size);
            char* data_pck = malloc(sizeof(DATA) - 1 + data_length);
            char* data_iter = data_pck;
            uint8_t pck_type = 4;
            memcpy(data_iter, &pck_type, sizeof(pck_type));
            data_iter += sizeof(pck_type);
            memcpy(data_iter, &session_id, sizeof(session_id));
            data_iter += sizeof(session_id);
            memcpy(data_iter, &pck_iter, sizeof(pck_iter));
            data_iter += sizeof(pck_iter);
            memcpy(data_iter, &curr_len, sizeof(curr_len));
            data_iter += sizeof(curr_len);
            memcpy(data_iter, data_ptr, curr_len);
            DATA* dt = (DATA*)data_pck;
            printf("Data length: %ld\n", data_length);
            printf("%ld\n", dt->session_id);
            printf("sex\n");

            // Send the package to the server.
            bytes_written = write_n_bytes(socket_fd, data_pck, sizeof(DATA) - 1 + data_length);
            if ((size_t) bytes_written < sizeof(*data_pck)) {
                error("Client failed to send a data package to the server.");
                close(socket_fd);
                return;
            }
            printf("%ld\n", bytes_written);
            ++pck_iter;
            data_ptr += curr_len;
            data_length -= curr_len;
        }

        printf("Finished sending data.\n");

        // Managed to send all the data, now we wait for the RCVD.
        RCVD recv_data_ack;
        assert_read(read_n_bytes(socket_fd, &recv_data_ack, sizeof(recv_data_ack)), sizeof(recv_data_ack));
    }

    printf("Client closing a connection\n");
    close(socket_fd);
}