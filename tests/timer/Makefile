CC     = gcc
CFLAGS = -Wall -Wextra -Wno-unknown-pragmas -Wno-unknown-warning-option \
         -I../../tests/driver_stubs \
         -I../../drivers/inc \
         -I../../chip_headers/CMSIS/Include \
         -I../../chip_headers/CMSIS/Device/ST/STM32F4xx/Include \
         -I../../3rd_party/unity/src \
         $(EXTRA_CFLAGS)

UNITY_SRC         = ../../3rd_party/unity/src/unity.c
TIMER_DRIVER_SRC  = ../../drivers/src/timer.c
RCC_DRIVER_SRC    = ../../drivers/src/rcc.c
TEST_PERIPH_SRC   = ../../tests/driver_stubs/test_periph.c

.PHONY: all run clean

all: test_timer.out

run: all
	./test_timer.out

test_timer.out: test_timer.c $(TIMER_DRIVER_SRC) $(RCC_DRIVER_SRC) \
                $(TEST_PERIPH_SRC) $(UNITY_SRC)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f *.out *.o *.gcda *.gcno
