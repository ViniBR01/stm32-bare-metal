CC     = gcc
CFLAGS = -Wall -Wextra -Wno-unknown-pragmas -Wno-unknown-warning-option \
         -DUNIT_TEST \
         -I../../tests/driver_stubs \
         -I../../drivers/inc \
         -I../../chip_headers/CMSIS/Include \
         -I../../chip_headers/CMSIS/Device/ST/STM32F4xx/Include \
         -I../../3rd_party/unity/src \
         $(EXTRA_CFLAGS)

UNITY_SRC       = ../../3rd_party/unity/src/unity.c
FLASH_DRIVER_SRC = ../../drivers/src/flash.c
TEST_PERIPH_SRC = ../../tests/driver_stubs/test_periph.c

.PHONY: all run clean

all: test_flash.out

run: all
	./test_flash.out

test_flash.out: test_flash.c $(FLASH_DRIVER_SRC) $(TEST_PERIPH_SRC) $(UNITY_SRC)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f *.out *.o *.gcda *.gcno
