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

#include "udp_server.h"
#include "protconst.h"
#include "common.h"
#include "err.h"

void run_udp_client(const struct sockaddr_in* server_addr, char* data, 
                    uint64_t data_length, uint64_t session_id) {
    // Ignore SIGPIPE signals.
    signal(SIGPIPE, SIG_IGN);

    // Create a socket.
    // Using server_addr directly caused problems, so I'm performing
    // a local copy of the sockaddr_in structure.
    struct sockaddr_in loc_server_addr = *server_addr;
    int socket_fd = create_socket(UDP_PROT_ID, data);

    struct timeval start, end;
    long long int send_data = 0;
    gettimeofday(&start, NULL);
    printf("Start; Session id: %ld\n", session_id);

    // Set timeouts for the server.
    set_timeouts(-1, socket_fd, data);

    // Send the CONN package.
    int flags = 0;
    socklen_t addr_length = (socklen_t)sizeof(*server_addr);
    CONN connection_data = {.pkt_type_id = CONN_TYPE, 
                            .session_id = session_id,
                            .prot_id = UDP_PROT_ID, 
                            .data_length = htobe64(data_length)};
    ssize_t bytes_written = sendto(socket_fd, &connection_data, 
                                    sizeof(connection_data),
                                    flags, (struct sockaddr*)&loc_server_addr,
                                    addr_length);
    bool b_connection_closed = assert_write
                                (bytes_written, sizeof(connection_data), 
                                socket_fd, -1, NULL, data);

    if (!b_connection_closed) {
        send_data += bytes_written;
        // Get the CONACC package.
        CONACC ack_pck;
        ssize_t bytes_read = recvfrom(socket_fd, &ack_pck,
                                        sizeof(ack_pck), flags,
                                        (struct sockaddr*)&loc_server_addr,
                                        &addr_length);
        b_connection_closed = assert_read(bytes_read, sizeof(ack_pck), 
                                            socket_fd, -1, NULL, data);
        if (!b_connection_closed) {
            b_connection_closed = get_connac_pck(&ack_pck, session_id);
        }

        // Send data to the server.
        uint64_t pck_number = 0;
        const char* data_ptr = data;
        while(data_length > 0 && !b_connection_closed) {
            // recvfrom can change the value of the addr_length,
            // so I have to update it here over and over again.
            addr_length = (socklen_t)sizeof(loc_server_addr);
            uint32_t curr_len = calc_pck_size(data_length);

            // Initialize a package.
            ssize_t pck_size = sizeof(DATA) - 8 + curr_len;
            char* data_pck = malloc(pck_size);
            assert_null(data_pck, socket_fd, -1, NULL, data);

            init_data_pck(session_id, pck_number, 
                                    curr_len, data_pck, data_ptr);

            bytes_written = sendto(socket_fd, data_pck, pck_size, flags,
                                    (struct sockaddr*)&loc_server_addr, 
                                    addr_length);
            b_connection_closed = assert_write(bytes_written, pck_size, 
                                                socket_fd, -1, data_pck, data);

            if (!b_connection_closed) {
                send_data += bytes_written;
                free(data_pck);
                ++pck_number;
                data_length -= curr_len;
                data_ptr += curr_len;
            }
        }
        if (!b_connection_closed) {
            // Get a RCVD package and finish execution.
            RCVD rcvd_pck;
            bytes_read = recvfrom(socket_fd, &rcvd_pck,
                                            sizeof(rcvd_pck), flags,
                                            (struct sockaddr*)&loc_server_addr,
                                            &addr_length);
            b_connection_closed = assert_read(bytes_read, sizeof(rcvd_pck), 
                                                socket_fd, -1, NULL, data);
            if (!b_connection_closed) {
                b_connection_closed = get_nonudpr_rcvd(&rcvd_pck, session_id);
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
    
    close(socket_fd);
}