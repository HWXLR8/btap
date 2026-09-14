CC=gcc
CFLAGS=-O2 -static -Wall
BIN=btap
# mister
MBIN=btap-arm
MCC=armv7a-unknown-linux-gnueabihf-gcc

all: $(BIN)

mister: $(MBIN)

$(MBIN): main.c
	$(MCC) $(CFLAGS) -o $@ $<

deploy: mister
	scp $(MBIN) root@mister:/media/fat/$(BIN)

$(BIN): main.c
	$(CC) $(CFLAGS) -o $@ $<

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN) $(MBIN) *.o

.PHONY: clean debug mister deploy
