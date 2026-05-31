CC      = gcc
CFLAGS  = -Wall -Wextra -Wno-unknown-pragmas -Wno-unknown-warning-option \
          -DUNIT_TEST \
          -I../../../lib/flash/inc \
          -I../../../lib/img/inc \
          -I../../../drivers/inc \
          -I../../driver_stubs \
          -I../../../chip_headers/CMSIS/Include \
          -I../../../chip_headers/CMSIS/Device/ST/STM32F4xx/Include \
          -I../../../3rd_party/unity/src \
          $(EXTRA_CFLAGS)

UNITY_SRC        = ../../../3rd_party/unity/src/unity.c
FLASH_SLOT_SRC   = ../../../lib/flash/src/flash_slot.c
FLASH_DRIVER_SRC = ../../../drivers/src/flash.c
TEST_PERIPH_SRC  = ../../driver_stubs/test_periph.c

.PHONY: all run clean

all: test_flash_slot.out

run: all
	./test_flash_slot.out

# Link the real flash driver (UNIT_TEST mode → fake buffer) so the
# slot-erase / commit-metadata calls have something to land in even
# when the host tests don't exercise them directly.
test_flash_slot.out: test_flash_slot.c $(FLASH_SLOT_SRC) $(FLASH_DRIVER_SRC) $(TEST_PERIPH_SRC) $(UNITY_SRC)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f *.out *.o *.gcda *.gcno
