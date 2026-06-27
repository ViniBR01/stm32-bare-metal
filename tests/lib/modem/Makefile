CC      = gcc
CFLAGS  = -Wall -Wextra -Wno-unknown-pragmas -Wno-unknown-warning-option \
          -I../../../lib/modem/inc \
          -I../../../lib/dsp/inc \
          -I../../../lib/prbs/inc \
          -I../../../3rd_party/unity/src \
          $(EXTRA_CFLAGS)

UNITY_SRC = ../../../3rd_party/unity/src/unity.c
BPSK_SRC  = ../../../lib/modem/src/bpsk.c
PRBS_SRC  = ../../../lib/prbs/src/prbs.c

.PHONY: all run clean

all: test_bpsk.out

run: all
	./test_bpsk.out

test_bpsk.out: test_bpsk.c $(BPSK_SRC) $(PRBS_SRC) $(UNITY_SRC)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f *.out *.gcda *.gcno
