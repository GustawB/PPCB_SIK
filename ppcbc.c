#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <endian.h>
#include <inttypes.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

#include "protconst.h"
#include "TCP_Client.h"
#include "common.h"
#include "err.h"

int main(int argc, char* argv[]) {
    if (argc != 4){
        fatal("usage: %s <protocol> <host> <port>", argv[0]);
    }
    else if (strcmp(argv[1], TCP_PROT) != 0 && strcmp(argv[1], UDP_PROT) &&
    strcmp(argv[1], UDPR_PROT) != 0) {
        fatal("Protocol %s is not supported.", argv[1]);
    }

    const char* host_name = argv[2];
    uint16_t port = read_port(argv[3]);
    struct sockaddr_in server_addr = get_server_address(host_name, port);
    run_tcp_client(&server_addr);
}