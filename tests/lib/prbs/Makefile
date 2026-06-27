CC      = gcc
CFLAGS  = -Wall -Wextra -Wno-unknown-pragmas -Wno-unknown-warning-option \
          -I../../../lib/prbs/inc \
          -I../../../3rd_party/unity/src \
          $(EXTRA_CFLAGS)

UNITY_SRC = ../../../3rd_party/unity/src/unity.c
PRBS_SRC  = ../../../lib/prbs/src/prbs.c

.PHONY: all run clean

all: test_prbs.out

run: all
	./test_prbs.out

test_prbs.out: test_prbs.c $(PRBS_SRC) $(UNITY_SRC)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f *.out *.gcda *.gcno
