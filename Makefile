CC = gcc
CFLAGS = -lpthread -lrt -g -O3 -Wall -Werror
SRC_DIR = src
BUILD_OUTPUT = out
BIN = client

deps = src/cpu.c

all: client

client:
	@mkdir -p $(BUILD_OUTPUT)
	$(CC) $(SRC_DIR)/client.c $(deps) -o $(BUILD_OUTPUT)/$@ $(CFLAGS)

clean:
	@rm -rf $(BUILD_OUTPUT)

.PHONY: all clean
