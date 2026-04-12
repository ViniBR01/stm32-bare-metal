CC      = gcc
CFLAGS  = -Wall -Wextra -Wno-unknown-pragmas -Wno-unknown-warning-option \
          -I./stubs \
          -I../../utils/inc \
          -I../../3rd_party/unity/src

UNITY_SRC = ../../3rd_party/unity/src/unity.c
CLI_SRC   = ../../utils/src/cli.c

.PHONY: all run clean

all: test_cli.out

run: all
	./test_cli.out

# cli.c is compiled with stubs/ first on the include path so that its
# #include "printf.h" and #include "printf_dma.h" resolve to the stubs.
cli.o: $(CLI_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

test_cli.out: test_cli.c cli.o $(UNITY_SRC)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f *.out *.o
