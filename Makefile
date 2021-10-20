#BIN=udpserver udpclient
CC=gcc
CFLAGS=-Wall -O0 -g
INCLUDE=-I .
LIBS=-lpthread
CFILES=$(wildcard ./*.c)
OBJS=$(patsubst %.c,%.o,$(CFILES))

all: server client

server: server.o
	$(CC) $(CFLAGS) -o server server.o

client: client.o cpu.o
	$(CC) $(CFLAGS) -o client client.o cpu.o $(LIBS)

$(OBJS): %.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<

clean:
	rm server client $(OBJS)

.PHONY: all clean
