CC      = gcc
# UNITY_INCLUDE_DOUBLE: BER values reach ~1e-3, so the theory/measured
# comparisons use double-precision Unity asserts (off by default).
CFLAGS  = -Wall -Wextra -Wno-unknown-pragmas -Wno-unknown-warning-option \
          -DUNITY_INCLUDE_DOUBLE \
          -I../../../lib/channel/inc \
          -I../../../lib/dsp/inc \
          -I../../../lib/modem/inc \
          -I../../../lib/prbs/inc \
          -I../../../3rd_party/unity/src \
          $(EXTRA_CFLAGS)

UNITY_SRC   = ../../../3rd_party/unity/src/unity.c
AWGN_SRC    = ../../../lib/channel/src/awgn.c
BPSK_SRC    = ../../../lib/modem/src/bpsk.c
PRBS_SRC    = ../../../lib/prbs/src/prbs.c

.PHONY: all run clean

all: test_awgn.out

run: all
	./test_awgn.out

test_awgn.out: test_awgn.c $(AWGN_SRC) $(BPSK_SRC) $(PRBS_SRC) $(UNITY_SRC)
	$(CC) $(CFLAGS) $^ -o $@ -lm

clean:
	rm -f *.out *.gcda *.gcno
