#BIN=udpserver udpclient
CC=gcc
CFLAGS=-Wall -O -g
INCLUDE=-I .
LIBS=
CFILES=$(wildcard ./*.c)
OBJS=$(patsubst %.c,%.o,$(CFILES))
TARGETS=$(CFILES:%.c=%)

all: $(TARGETS)

$(TARGETs): %: %.o
	$(CC) $(LIBS) -o $@ $<

$(OBJS): %.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<

clean:
	@rm $(TARGETS) $(OBJS)

.PHONY: all clean
