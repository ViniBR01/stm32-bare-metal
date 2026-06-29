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
IMPAIR_SRC  = ../../../lib/channel/src/impair.c
BPSK_SRC    = ../../../lib/modem/src/bpsk.c
PRBS_SRC    = ../../../lib/prbs/src/prbs.c
SINCOS_SRC  = ../../../lib/dsp/src/sincos.c
NCO_SRC     = ../../../lib/dsp/src/nco.c

.PHONY: all run clean

all: test_awgn.out test_impair.out

run: all
	./test_awgn.out
	./test_impair.out

test_awgn.out: test_awgn.c $(AWGN_SRC) $(BPSK_SRC) $(PRBS_SRC) $(UNITY_SRC)
	$(CC) $(CFLAGS) $^ -o $@ -lm

# Impairments pull in the NCO/sincos (lib/dsp) and the cq15 AWGN variant.
test_impair.out: test_impair.c $(IMPAIR_SRC) $(AWGN_SRC) $(SINCOS_SRC) $(NCO_SRC) $(UNITY_SRC)
	$(CC) $(CFLAGS) -I../../../lib/dsp/src $^ -o $@ -lm

clean:
	rm -f *.out *.gcda *.gcno
