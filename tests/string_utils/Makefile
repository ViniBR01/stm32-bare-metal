CC      = gcc
CFLAGS  = -Wall -Wextra -Wno-unknown-pragmas -Wno-unknown-warning-option \
          -I../../utils/inc \
          -I../../3rd_party/log_c/3rd-party/unity/src

UNITY_SRC = ../../3rd_party/log_c/3rd-party/unity/src/unity.c

.PHONY: all run clean

all: test_string_utils.out

run: all
	./test_string_utils.out

test_string_utils.out: test_string_utils.c $(UNITY_SRC)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f *.out
