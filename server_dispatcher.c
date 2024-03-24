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
#include "TCP_Server.h"
#include "common.h"
#include "err.h"

int main(int argc, char* argv[]) {
    if (argc != 3){
        fatal("Usage: %s <protocol> <port>", argv[0]);
    }
    else if (strcmp(argv[1], TCP_PROT) != 0 && strcmp(argv[1], UDP_PROT)){
        fatal("Protocol %s is not supported.", argv[1]);
    }

    uint16_t port = read_port(argv[2]);

    run_tcp_server(port);
}