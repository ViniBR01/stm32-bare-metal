CC     = gcc
CFLAGS = -Wall -Wextra -Wno-unknown-pragmas -Wno-unknown-warning-option \
         -I../../tests/driver_stubs \
         -I../../drivers/inc \
         -I../../chip_headers/CMSIS/Include \
         -I../../chip_headers/CMSIS/Device/ST/STM32F4xx/Include \
         -I../../3rd_party/unity/src \
         $(EXTRA_CFLAGS)

UNITY_SRC       = ../../3rd_party/unity/src/unity.c
CRC_DRIVER_SRC  = ../../drivers/src/crc.c
TEST_PERIPH_SRC = ../../tests/driver_stubs/test_periph.c

.PHONY: all run clean

all: test_crc.out

run: all
	./test_crc.out

test_crc.out: test_crc.c $(CRC_DRIVER_SRC) $(TEST_PERIPH_SRC) $(UNITY_SRC)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f *.out *.o *.gcda *.gcno
