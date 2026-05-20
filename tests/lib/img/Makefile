CC      = gcc
CFLAGS  = -Wall -Wextra -Wno-unknown-pragmas -Wno-unknown-warning-option \
          -I../../../lib/img/inc \
          -I../../../3rd_party/unity/src \
          $(EXTRA_CFLAGS)

UNITY_SRC = ../../../3rd_party/unity/src/unity.c
IMG_SRC = ../../../lib/img/src/img_header.c

.PHONY: all run clean

all: test_img_header.out

run: all
	./test_img_header.out

test_img_header.out: test_img_header.c $(IMG_SRC) $(UNITY_SRC)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f *.out *.gcda *.gcno
