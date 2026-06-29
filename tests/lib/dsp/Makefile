CC      = gcc
# UNITY_INCLUDE_DOUBLE: the BER-vs-theory asserts in test_rrc use doubles.
CFLAGS  = -Wall -Wextra -Wno-unknown-pragmas -Wno-unknown-warning-option \
          -DUNITY_INCLUDE_DOUBLE \
          -I../../../lib/dsp/inc \
          -I../../../lib/dsp/src \
          -I../../../lib/modem/inc \
          -I../../../lib/prbs/inc \
          -I../../../lib/channel/inc \
          -I../../../3rd_party/unity/src \
          $(EXTRA_CFLAGS)

UNITY_SRC   = ../../../3rd_party/unity/src/unity.c
RRC_SRC     = ../../../lib/dsp/src/rrc.c
SINCOS_SRC  = ../../../lib/dsp/src/sincos.c
NCO_SRC     = ../../../lib/dsp/src/nco.c
BPSK_SRC    = ../../../lib/modem/src/bpsk.c
PRBS_SRC    = ../../../lib/prbs/src/prbs.c
AWGN_SRC    = ../../../lib/channel/src/awgn.c

.PHONY: all run clean

all: test_fixed.out test_rrc.out test_complexq15.out test_sincos.out

run: all
	./test_fixed.out
	./test_rrc.out
	./test_complexq15.out
	./test_sincos.out

# fixed.h is header-only (static inline), so only the test + Unity compile.
test_fixed.out: test_fixed.c $(UNITY_SRC)
	$(CC) $(CFLAGS) $^ -o $@

# RRC pulse shaping pulls in the modem/prbs/channel libs for the BER chain.
test_rrc.out: test_rrc.c $(RRC_SRC) $(BPSK_SRC) $(PRBS_SRC) $(AWGN_SRC) $(UNITY_SRC)
	$(CC) $(CFLAGS) $^ -o $@ -lm

# complexq15.h is header-only (static inline).
test_complexq15.out: test_complexq15.c $(UNITY_SRC)
	$(CC) $(CFLAGS) $^ -o $@ -lm

# sincos LUT + NCO.
test_sincos.out: test_sincos.c $(SINCOS_SRC) $(NCO_SRC) $(UNITY_SRC)
	$(CC) $(CFLAGS) $^ -o $@ -lm

clean:
	rm -f *.out *.gcda *.gcno
