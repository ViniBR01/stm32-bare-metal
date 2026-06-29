CC      = gcc
# UNITY_INCLUDE_DOUBLE: the e2e BER asserts use doubles.
CFLAGS  = -Wall -Wextra -Wno-unknown-pragmas -Wno-unknown-warning-option \
          -DUNITY_INCLUDE_DOUBLE \
          -I../../../lib/sync/inc \
          -I../../../lib/dsp/inc \
          -I../../../lib/dsp/src \
          -I../../../lib/modem/inc \
          -I../../../lib/prbs/inc \
          -I../../../lib/channel/inc \
          -I../../../3rd_party/unity/src \
          $(EXTRA_CFLAGS)

UNITY_SRC   = ../../../3rd_party/unity/src/unity.c
AGC_SRC     = ../../../lib/sync/src/agc.c
BARKER_SRC  = ../../../lib/sync/src/barker.c
TIMING_SRC  = ../../../lib/sync/src/timing_mm.c
COSTAS_SRC  = ../../../lib/sync/src/costas.c
SINCOS_SRC  = ../../../lib/dsp/src/sincos.c
NCO_SRC     = ../../../lib/dsp/src/nco.c
RRC_SRC     = ../../../lib/dsp/src/rrc.c
BPSK_SRC    = ../../../lib/modem/src/bpsk.c
PRBS_SRC    = ../../../lib/prbs/src/prbs.c
AWGN_SRC    = ../../../lib/channel/src/awgn.c
IMPAIR_SRC  = ../../../lib/channel/src/impair.c

.PHONY: all run clean

all: test_agc.out test_barker.out test_timing_mm.out test_costas.out test_recover_e2e.out

run: all
	./test_agc.out
	./test_barker.out
	./test_timing_mm.out
	./test_costas.out
	./test_recover_e2e.out

test_agc.out: test_agc.c $(AGC_SRC) $(UNITY_SRC)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test_barker.out: test_barker.c $(BARKER_SRC) $(UNITY_SRC)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test_timing_mm.out: test_timing_mm.c $(TIMING_SRC) $(SINCOS_SRC) $(NCO_SRC) \
                    $(RRC_SRC) $(BPSK_SRC) $(PRBS_SRC) $(IMPAIR_SRC) $(UNITY_SRC)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test_costas.out: test_costas.c $(COSTAS_SRC) $(SINCOS_SRC) $(NCO_SRC) \
                 $(BPSK_SRC) $(PRBS_SRC) $(UNITY_SRC)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test_recover_e2e.out: test_recover_e2e.c $(AGC_SRC) $(BARKER_SRC) $(TIMING_SRC) \
                      $(COSTAS_SRC) $(SINCOS_SRC) $(NCO_SRC) $(RRC_SRC) $(BPSK_SRC) \
                      $(PRBS_SRC) $(AWGN_SRC) $(IMPAIR_SRC) $(UNITY_SRC)
	$(CC) $(CFLAGS) $^ -o $@ -lm

clean:
	rm -f *.out *.gcda *.gcno
