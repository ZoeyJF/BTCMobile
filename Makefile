CC = gcc
CFLAGS = -O3 -std=c11 -Wall -Wextra -pthread
TARGETS = CREATE-BiX BTC

all: $(TARGETS)

CREATE-BiX: CREATE-BiX.c
	$(CC) $(CFLAGS) $< -o $@

BTC: BTC.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TARGETS) *.o BTC.bin BTC.idx BTC.xor FOUND.txt

.PHONY: all clean
