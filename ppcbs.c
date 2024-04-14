#include "common.h"
#include "protconst.h"
#include "tcp_server.h"
#include "udp_server.h"
#include "err.h"

int main(int argc, char* argv[]) {
    if (argc != 3){
        fatal("Usage: %s <protocol> <port>", argv[0]);
    }
    else if (strcmp(argv[1], TCP_PROT) != 0 && strcmp(argv[1], UDP_PROT)){
        fatal("Protocol %s is not supported.", argv[1]);
    }

    uint16_t port = read_port(argv[2]);

    // Server dispatching.
    if (strcmp(argv[1], TCP_PROT) == 0) {
        run_tcp_server(port);
    }
    else {
        run_udp_server(port);
    }
}