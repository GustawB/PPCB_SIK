CC     = gcc
CFLAGS = -Wall -Wextra -O2 -std=gnu17
LFLAGS =

.PHONY: all clean

TARGET1 = ppcbc
TARGET2 = ppcbs

all: $(TARGET1) $(TARGET2)

$(TARGET1): $(TARGET1).o err.o common.o
$(TARGET2): $(TARGET2).o err.o TCP_Server.o common.o

err.o: err.c err.h
common.o: common.c common.h

TCP_Server.o: TCP_Server.c TCP_Server.h err.h common.h

ppcbc.o: ppcbc.c err.h protconst.h common.h
ppcbs.o: ppcbs.c err.h protconst.h common.h

clean:
	rm -f $(TARGET1) $(TARGET2) *.o *~