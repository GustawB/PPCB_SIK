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

#include "tcp_server.h"
#include "protconst.h"
#include "common.h"
#include "err.h"

void run_tcp_client(struct sockaddr_in* server_addr, const char* data, 
                    uint64_t data_length, uint64_t session_id) {
    // Ignore SIGPIPE signals; stays for now.
    signal(SIGPIPE, SIG_IGN);

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

    // Send a CONN package to mark the beginning of the connection.
    CONN connect_data = {.pkt_type_id = CONN_TYPE, .session_id = session_id, .prot_id = TCP_PROT_ID,
                         .data_length = htobe64(data_length)};
    ssize_t bytes_written = write_n_bytes(socket_fd, &connect_data, sizeof(connect_data));
    if ((size_t) bytes_written < sizeof(connect_data)) {
        error("Client failed to send a CONN package to the server.");
        close(socket_fd);
        return;
    }

    // Read a CONACC package but only if we managed to send the CONN package.
    CONACC con_ack_data;
    ssize_t bytes_read = read_n_bytes(socket_fd, &con_ack_data, 
                            sizeof(con_ack_data));

    // If w managed to both send CONN and receive CONACK, we can proceed
    // to the data transfer.
    sleep(10);
    if (assert_read(bytes_read, sizeof(con_ack_data))) {
        printf("Sending data...\n");

        uint64_t pck_number = 0;
        const char* data_ptr = data;
        while(data_length > 0) {
            // Calculate a size of the data chunk that will be sent
            // in the current package.
            uint32_t curr_len = PCK_SIZE;
            if (curr_len > data_length) {
                curr_len = data_length;
            }

            // Initialize a package.
            size_t pck_size = sizeof(DATA) - 8 + data_length;
            char* data_pck = malloc(pck_size);
            if (data_pck == NULL) { 
                // malloc failed.
                break;
            }
            init_data_pck(session_id, pck_number, 
                                    data_length, data_pck, data);

            // Send the package to the server.
            printf("Package size: %ld\n", pck_size);
            bytes_written = write_n_bytes(socket_fd, data_pck, pck_size);
            printf("Write size: %ld\n", bytes_written);
            if ((size_t) bytes_written < sizeof(*data_pck)) {
                error("Client failed to send a data package to the server.");
                close(socket_fd);
                return;
            }
            
            // Update invariants.
            ++pck_number;
            data_ptr += curr_len;
            data_length -= curr_len;
        }

        // Managed to send all the data, now we wait for the RCVD.
        RCVD recv_data_ack;
        bytes_read = read_n_bytes(socket_fd, &recv_data_ack,
                sizeof(recv_data_ack));

        if (!assert_read(bytes_read, sizeof(recv_data_ack))) {
            error("Failed to receive RCVD package");
        }
    }

    close(socket_fd);
}