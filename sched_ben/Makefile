BIN=linux_ben
CC=gcc
CFLAGS=-Wall -O3 -g
INCLUDE=-I .
LIBS=
CFILES=$(wildcard ./*.c)
TARGET=$(patsubst %.c,%.o,$(CFILES))

all: $(BIN)

$(TARGET): %.o: %.c
	$(CC) -c $< -o $@ $(INCLUDE) $(CFLAGS)

$(BIN):$(TARGET)
	$(CC) -o $(BIN) $(TARGET) $(LIBS) $(CFLAGS)

clean:
	@rm $(BIN) $(TARGET)
