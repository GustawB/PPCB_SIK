CC     = gcc
CFLAGS = -Wall -Wextra -O2 -std=gnu17
LFLAGS =

.PHONY: all clean

TARGET1 = ppcbc
TARGET2 = ppcbs

all: $(TARGET1) $(TARGET2)

$(TARGET1): $(TARGET1).o err.o tcp_client.o udp_client.o udpr_client.o common.o
$(TARGET2): $(TARGET2).o err.o tcp_server.o udp_server.o  common.o

err.o: err.c err.h
common.o: common.c common.h protconst.h

tcp_server.o: tcp_server.c tcp_server.h err.h common.h
tcp_client.o: tcp_client.c tcp_client.h err.h common.h

udp_server.o: udp_server.c udp_server.h err.h common.h
udp_client.o: udp_client.c udp_client.h err.h common.h

udpr_client.o: udpr_client.c udpr_client.h err.h common.h

ppcbc.o: ppcbc.c err.h protconst.h common.h
ppcbs.o: ppcbs.c err.h protconst.h common.h

clean:
	rm -f $(TARGET1) $(TARGET2) *.o *~