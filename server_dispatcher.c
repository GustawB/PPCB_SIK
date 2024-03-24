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
#include "common.h"
#include "err.h"

int main(int argc, char* argv[]) {
    if (argc != 3){
        fatal("usage: %s <protocol> <port>", argv[0]);
    }
    else if (strcmp(argv[1], TCP_PROT) != 0 && strcmp(argv[1], UDP_PROT)){
        fatal("Protocol %s is not supported.", argv[1]);
    }

}