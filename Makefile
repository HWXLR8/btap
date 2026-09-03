# CC=armv7a-unknown-linux-gnueabihf-gcc
CC=gcc
CFLAGS=-O2 -static -Wall
BIN=btap

all: $(BIN)

$(BIN): main.c
	$(CC) $(CFLAGS) -o $@ $<

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN) *.o

.PHONY: clean debug
