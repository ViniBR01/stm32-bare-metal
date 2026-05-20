CC      = gcc
CFLAGS  = -Wall -Wextra -Wno-unknown-pragmas -Wno-unknown-warning-option \
          -I../../../lib/skeleton/inc \
          -I../../../3rd_party/unity/src \
          $(EXTRA_CFLAGS)

UNITY_SRC = ../../../3rd_party/unity/src/unity.c
SKELETON_SRC = ../../../lib/skeleton/src/skeleton.c

.PHONY: all run clean

all: test_skeleton.out

run: all
	./test_skeleton.out

test_skeleton.out: test_skeleton.c $(SKELETON_SRC) $(UNITY_SRC)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f *.out *.gcda *.gcno
