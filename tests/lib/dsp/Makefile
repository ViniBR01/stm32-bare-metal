CC      = gcc
CFLAGS  = -Wall -Wextra -Wno-unknown-pragmas -Wno-unknown-warning-option \
          -I../../../lib/dsp/inc \
          -I../../../3rd_party/unity/src \
          $(EXTRA_CFLAGS)

UNITY_SRC = ../../../3rd_party/unity/src/unity.c

.PHONY: all run clean

all: test_fixed.out

run: all
	./test_fixed.out

# fixed.h is header-only (static inline), so only the test + Unity compile.
test_fixed.out: test_fixed.c $(UNITY_SRC)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f *.out *.gcda *.gcno
