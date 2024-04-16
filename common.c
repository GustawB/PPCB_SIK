#include "common.h"
#include "err.h"
#include "protconst.h"

void init_data_pck(uint64_t session_id, uint64_t pck_number, 
                    uint32_t data_size, char* data_pck, const char* data) {
    uint8_t pck_type = DATA_TYPE;
    char* data_iter = data_pck;

    memcpy(data_iter, &pck_type, sizeof(pck_type));
    data_iter += sizeof(pck_type);

    memcpy(data_iter, &session_id, sizeof(session_id));
    data_iter += sizeof(session_id);

    memcpy(data_iter, &pck_number, sizeof(pck_number));
    data_iter += sizeof(pck_number);

    memcpy(data_iter, &data_size, sizeof(data_size));
    data_iter += sizeof(data_size);

    memcpy(data_iter, data, data_size);
}

void init_sockaddr(struct sockaddr_in* addr, uint16_t port) {
    addr->sin_family = AF_INET; // IPv4 protocol.
    addr->sin_addr.s_addr = htonl(INADDR_ANY); // Listening on all interfaces.
    addr->sin_port = htons(port);
}

uint16_t read_port(char const *string) {
    char *endptr;
    unsigned long port = strtoul(string, &endptr, 10);
    if ((port == ULONG_MAX && errno == ERANGE) || *endptr != 0 || 
        port == 0 || port > UINT16_MAX) {
        fatal("%s is not a valid port number.", string);
    }
    return (uint16_t) port;
}

ssize_t read_n_bytes(int fd, void* dsptr, size_t n) {
    ssize_t bytes_read;
    ssize_t bytes_left = n;
    char* iter_ptr = dsptr;

    // Read stream as long as there is no error and we didn't read everything.
    while (bytes_left > 0) {
        if ((bytes_read = read(fd, iter_ptr, bytes_left)) < 0) {
            // read() failed, return < 0.
            return bytes_read;
        }
        else if (bytes_read == 0) {
            // Encountered EOF.
            break;
        }
        bytes_left -= bytes_read;
        iter_ptr += bytes_read;
    }

    return n - bytes_left;
}

ssize_t write_n_bytes(int fd, void* dsptr, size_t n) {
    ssize_t bytes_written;
    ssize_t bytes_left = n;
    const char* iter_ptr = dsptr;

    // Write to the stream as long as there is 
    // no error and we didn't write everything.
    while(bytes_left > 0) {
        if ((bytes_written = write(fd, iter_ptr, bytes_left)) <= 0) {
            // There was some kind of error.
            return bytes_written;
        }

        bytes_left -= bytes_written;
        iter_ptr += bytes_written;
    }

    return n - bytes_left;
}

struct sockaddr_in get_server_address(char const *host, 
                                        uint16_t port, int8_t protocol_id) {
    struct addrinfo hints;
    // Malloc would require me to worry about freeing the memory later,
    // and I don't need pointer arithmetics.
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    if (protocol_id == TCP_PROT_ID) {
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
    }
    else {
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;
    }

    struct addrinfo* addr_res;
    int errcode = getaddrinfo(host, NULL, &hints, &addr_res);
    if (errcode != 0) {
        fatal("getaddrinfo: %s", gai_strerror(errcode));
    }

    struct sockaddr_in send_addr;
    send_addr.sin_family = AF_INET; // IPv4;
    // IP address.
    send_addr.sin_addr.s_addr = 
        ((struct sockaddr_in*)addr_res->ai_addr)->sin_addr.s_addr;
    send_addr.sin_port = htons(port);

    freeaddrinfo(addr_res);

    return send_addr;
}

void cleanup(char* data_to_cleanup) {
    if (data_to_cleanup != NULL) {
        free(data_to_cleanup);
    }
}

void close_fd(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

bool assert_write(ssize_t result, ssize_t to_cmp, 
                    int main_fd, int secondary_fd, 
                    char* data_to_cleanup, char* data_from_stream) {
    if(result < 0) {
        cleanup(data_to_cleanup);
        if (errno == EPIPE) {
            // Connection closed. Maninly for servers.
            close_fd(secondary_fd);
            error("Connection closed.");
            errno = 0;
            return true;
        }
        else {
            close_fd(main_fd);
            close_fd(secondary_fd);
            cleanup(data_from_stream);
            syserr("Package send failed");
        }
    }
    else if (result !=  to_cmp) {
        close_fd(secondary_fd);
        cleanup(data_to_cleanup);
        error("Incomplete send");
        return true;
    }
    return false;
}

bool assert_read(ssize_t result, ssize_t to_cmp, int main_fd,
                 int secondary_fd, char* data_to_cleanup,
                 char* data_from_stream) {
    if (result < 0) {
        cleanup(data_to_cleanup);
        if (errno == EAGAIN) {
            // Connection timeout.
            close_fd(secondary_fd);
            error("Connection timeout");
            errno = 0;
            return true;
        }
        else {
            close_fd(main_fd);
            close_fd(secondary_fd);
            cleanup(data_from_stream);
            syserr("Failed to write data");
        }
    }
    else if (result == 0) {
        // Connection closed.
        cleanup(data_to_cleanup);
        close_fd(secondary_fd);
        error("Connection closed");
        return true;
    }
    else if (result != to_cmp) {
        cleanup(data_to_cleanup);
        close_fd(secondary_fd);
        error("Incomplete read");
        return true;
    }
    return false;
}

void assert_null(char* data, int main_fd, int secondary_fd,
                 char* data_to_cleanup, char* data_from_stream) {
    if (data == NULL) {
        close_fd(main_fd);
        close_fd(secondary_fd);
        cleanup(data_to_cleanup);
        cleanup(data_from_stream);
        fatal("Malloc failed");
    }
}

void print_data(char* data, size_t len) {
    fwrite(data, sizeof(char), len, stdout);
    fflush(stdout);
}

uint32_t calc_pck_size(uint64_t data_length) {
    // Calculate a size of the data chunk that will be
    // send received.
    uint32_t curr_len = PCK_SIZE;
    if (curr_len > data_length) {
        curr_len = data_length;
    }
    return curr_len;
}

bool get_connac_pck(const CONACC* ack_pck, uint64_t session_id) {
    if (ack_pck->pkt_type_id == CONRJT_TYPE && 
        ack_pck->session_id == session_id) {
        // We got rejected by the server.
        error("Connection rejected");
        return true;
    }
    else if (ack_pck->pkt_type_id != CONACC_TYPE ||
            ack_pck->session_id != session_id) {
        // We got something invalid, end with error.
        error("Invalid package");
        return true;
    }

    return false;
}

bool get_nonudpr_rcvd(const RCVD* rcvd_pck, uint64_t session_id) {
    if (rcvd_pck->pkt_type_id == RJT_TYPE && 
        rcvd_pck->session_id == session_id) {
        // We got rejected.
        errno = 0;
        error("Data Rejected");
        return true;
    }
    else if (rcvd_pck->pkt_type_id != RCVD_TYPE ||
        rcvd_pck->session_id != session_id) {
        // We received invalid package.
        error("Invalid package");
        return true;
    }

    return false;
}

int create_socket(uint8_t protocol_id, char* data_from_stream) {
    // Create a socket with IPv4 protocol.
    int socket_fd = -1;
    if (protocol_id == TCP_PROT_ID) {
        socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    }
    else if (protocol_id == UDP_PROT_ID || protocol_id == UDPR_PROT_ID) {
        socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    }

    if(socket_fd < 0){
        cleanup(data_from_stream);
        syserr("Failed to create a socket.");
    }

    return socket_fd;
}

int setup_socket(struct sockaddr_in* addr, uint8_t protocol_id, 
                    uint16_t port, char* data_from_stream) {
    // Create a socket with IPv4 protocol.
    int socket_fd = create_socket(protocol_id, data_from_stream);

    // Bind the socket to the local adress.
    init_sockaddr(addr, port);
    if (bind(socket_fd, (struct sockaddr*)addr,
         (socklen_t) sizeof(*addr)) < 0){
        close(socket_fd);
        cleanup(data_from_stream);
        syserr("ERROR: Failed to bind a socket");
    }

    return socket_fd;
}

void set_timeouts(int main_fd, int secondary_fd,
                     char* data_from_stream) {
    struct timeval time_options = {.tv_sec = MAX_WAIT, .tv_usec = 0};
    if (setsockopt(secondary_fd, SOL_SOCKET, SO_RCVTIMEO,
        &time_options, sizeof(time_options)) < 0) {
        close_fd(main_fd);
        close_fd(secondary_fd);
        cleanup(data_from_stream);
        syserr("Failed to set timeouts");
    }
    
    if (setsockopt(secondary_fd, SOL_SOCKET, SO_SNDTIMEO, 
        &time_options, sizeof(time_options)) < 0) {
        close_fd(main_fd);
        close_fd(secondary_fd);
        cleanup(data_from_stream);
        syserr("Failed to set timeouts");
    }

}

void ignore_signal(void (*handler)(), int8_t signtoign) {
    struct sigaction action;
    sigset_t block_mask;

    if (sigemptyset(&block_mask) < 0) {
        syserr("sigemptyset failed");
    }

    if (handler == NULL) {
        action.sa_handler = SIG_IGN;
    }
    else {
        action.sa_handler = handler;
    }
    action.sa_mask = block_mask;
    action.sa_flags = 0;

    if (sigaction(signtoign, &action, 0) < 0) {
        syserr("sigaction failed");
    }
}