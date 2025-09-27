CC = arm-none-eabi-gcc
CFLAGS += -c -mcpu=cortex-m4 -mthumb -std=gnu11
CFLAGS += -DSTM32F411xE
BLINK_MAP = -Wl,-Map=blink_led_simple.map
PUSHBTN_MAP = -Wl,-Map=push_button_simple.map
LDFLAGS = -nostdlib -T stm32_ls.ld
INCLUDES += -I./chip_headers/CMSIS/Include
INCLUDES += -I./chip_headers/CMSIS/Device/ST/STM32F4xx/Include

all: blink_led_simple.elf push_button_simple.elf 

blink_led_simple.o : blink_led_simple.c
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@

push_button_simple.o : push_button_simple.c
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@
	
stm32f411_startup.o : stm32f411_startup.c
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@
	
blink_led_simple.elf : blink_led_simple.o stm32f411_startup.o
	$(CC) $(LDFLAGS) $(BLINK_MAP) $^ -o $@

push_button_simple.elf : push_button_simple.o stm32f411_startup.o
	$(CC) $(LDFLAGS) $(PUSHBTN_MAP) $^ -o $@

openocd:
	openocd -f board/st_nucleo_f4.cfg

clean:
	rm -f *.o *.elf *.map