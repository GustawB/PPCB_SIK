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

void run_tcp_client(struct sockaddr_in* server_addr, char* data, 
                    uint64_t data_length, uint64_t session_id) {
    // Ignore SIGPIPE signals; stays for now.
    signal(SIGPIPE, SIG_IGN);

    // Create a socket with IPv4 protocol.
    int socket_fd = create_socket(TCP_PROT_ID, data);

    // Connect to the server.
    if (connect(socket_fd, (struct sockaddr*)server_addr,
                (socklen_t) sizeof(*server_addr)) < 0) {
        close(socket_fd);
        free(data);
        syserr("Client failed to connect to the server");
    }

    long long int send_data = 0;
    clock_t tic = clock();

    // Set timeouts for the server.
    set_timeouts(-1, socket_fd, data);

    // Send a CONN package to mark the beginning of the connection.
    CONN connect_data = {.pkt_type_id = CONN_TYPE, .session_id = session_id, .prot_id = TCP_PROT_ID,
                         .data_length = htobe64(data_length)};
    ssize_t bytes_written = write_n_bytes(socket_fd, &connect_data, sizeof(connect_data));
    bool b_connection_close = assert_write(bytes_written, sizeof(connect_data), socket_fd, -1, NULL, data);
    
    CONACC con_ack_data;
    if (!b_connection_close){
        send_data += bytes_written;
        // Read a CONACC package but only if we managed to send the CONN package.
        ssize_t bytes_read = read_n_bytes(socket_fd, &con_ack_data, 
                                sizeof(con_ack_data));
        ssize_t result = get_connac_pck(socket_fd, &con_ack_data, bytes_read, session_id, data);
        if(result <= 0) {
            b_connection_close = true;
        }
    }

    // If w managed to both send CONN and receive CONACK, we can proceed
    // to the data transfer.
    if (!b_connection_close && con_ack_data.pkt_type_id == CONACC_TYPE && 
        con_ack_data.session_id == session_id) {
        uint64_t pck_number = 0;
        const char* data_ptr = data;
        while(data_length > 0 && !b_connection_close) {
            uint32_t curr_len = calc_pck_size(data_length);

            // Initialize a package.
            size_t pck_size = sizeof(DATA) - 8 + curr_len;
            char* data_pck = malloc(pck_size);
            //assert_malloc(data_pck, socket_fd, -1, NULL, data);
            
            init_data_pck(session_id, pck_number, 
                                    curr_len, data_pck, data_ptr);

            // Send the package to the server.
            bytes_written = write_n_bytes(socket_fd, data_pck, pck_size);
            b_connection_close = assert_write(bytes_written, pck_size, socket_fd, -1, data_pck, data);
            
            if (!b_connection_close) {
                send_data += bytes_written;
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
            get_nonudpr_rcvd(socket_fd, &recv_data_ack, bytes_read, session_id, data);
            //printf("%d %ld\n", recv_data_ack.pkt_type_id, recv_data_ack.session_id);
        }
    }

    if (DEBUG_STATE == 1) {
        clock_t toc = clock();
        printf("Elapsed: %f seconds\n", (double)(toc - tic) / CLOCKS_PER_SEC);
        printf("Bytes send in total: %lld\n", send_data);
    }
    
    close(socket_fd);
}