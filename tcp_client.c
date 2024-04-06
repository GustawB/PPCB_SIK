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
        syserr("Failed to create a socket.");
    }

    printf("%d\n", server_addr->sin_addr.s_addr);
    // Connect to the server.
    if (connect(socket_fd, (struct sockaddr*)server_addr,
                (socklen_t) sizeof(*server_addr)) < 0) {
        syserr("Client failed to connect to the server");
    }

    // Set timeouts for the server.
    struct timeval time_options = {.tv_sec = MAX_WAIT, .tv_usec = 0};
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &time_options, sizeof(time_options));
    setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &time_options, sizeof(time_options));

    // Send a CONN package to mark the beginning of the connection.
    CONN connect_data = {.pkt_type_id = CONN_TYPE, .session_id = session_id, .prot_id = TCP_PROT_ID,
                         .data_length = htobe64(data_length)};
    ssize_t bytes_written = write_n_bytes(socket_fd, &connect_data, sizeof(connect_data));
    bool b_connection_close = assert_write(bytes_written, sizeof(connect_data), socket_fd, -1, NULL);
    
    CONACC con_ack_data;
    if (!b_connection_close){
        // Read a CONACC package but only if we managed to send the CONN package.
        ssize_t bytes_read = read_n_bytes(socket_fd, &con_ack_data, 
                                sizeof(con_ack_data));
        b_connection_close = assert_read(bytes_read, sizeof(con_ack_data), socket_fd, -1, NULL);
        if (!b_connection_close && con_ack_data.pkt_type_id == CONRJT_TYPE && 
            con_ack_data.session_id == session_id) {
            // We got rejected.
            error("Connection Rejected"); 
        }
        else if (!b_connection_close && (con_ack_data.pkt_type_id != CONACC_TYPE ||
            con_ack_data.session_id != session_id)) {
            // We received invalid package.
            error("Invalid package");
        }
    }

    // If w managed to both send CONN and receive CONACK, we can proceed
    // to the data transfer.
    if (!b_connection_close && con_ack_data.pkt_type_id == CONACC_TYPE && 
        con_ack_data.session_id == session_id) {
        printf("Sending data...\n");

        uint64_t pck_number = 0;
        const char* data_ptr = data;
        while(data_length > 0 && !b_connection_close) {
            // Calculate a size of the data chunk that will be sent
            // in the current package.
            uint32_t curr_len = PCK_SIZE;
            if (curr_len > data_length) {
                curr_len = data_length;
            }

            // Initialize a package.
            size_t pck_size = sizeof(DATA) - 8 + curr_len;
            char* data_pck = malloc(pck_size);
            assert_malloc(data_pck, socket_fd, -1, NULL);
            
            init_data_pck(session_id, pck_number, 
                                    data_length, data_pck, data_ptr);

            // Send the package to the server.
            bytes_written = write_n_bytes(socket_fd, data_pck, pck_size);
            b_connection_close = assert_write(bytes_written, pck_size, socket_fd, -1, data_pck);
            
            if (!b_connection_close) {
                // Update invariants.
                ++pck_number;
                data_ptr += curr_len;
                data_length -= curr_len;
                free(data_pck);
            }
        }

        if (!b_connection_close) {
            // Managed to send all the data, now we wait for the RCVD.
            RCVD recv_data_ack;
            ssize_t bytes_read = read_n_bytes(socket_fd, &recv_data_ack,
                    sizeof(recv_data_ack));
            b_connection_close = assert_read(bytes_read, sizeof(recv_data_ack), socket_fd, -1, NULL);
            if (!b_connection_close && recv_data_ack.pkt_type_id == RJT_TYPE && 
                recv_data_ack.session_id == session_id) {
                // We got rejected.
                error("Data Rejected"); 
            }
            else if (!b_connection_close && (recv_data_ack.pkt_type_id != RCVD_TYPE ||
                recv_data_ack.session_id != session_id)) {
                // We received invalid package.
                error("Invalid package");
            }
        }
    }
    
    close(socket_fd);
}