CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -march=native
LDFLAGS = -lm -ldl

TARGETS = sender receiver threshold

.PHONY: all clean

all: $(TARGETS)

sender: sender.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

receiver: receiver.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

threshold: threshold.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGETS)
