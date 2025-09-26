CC = arm-none-eabi-gcc
CFLAGS += -c -mcpu=cortex-m4 -mthumb -std=gnu11
CFLAGS += -DSTM32F411xE
LDFLAGS = -nostdlib -T stm32_ls.ld -Wl,-Map=blink_led.map
INCLUDES += -I./chip_headers/CMSIS/Include
INCLUDES += -I./chip_headers/CMSIS/Device/ST/STM32F4xx/Include

all: blink_led.elf 

main.o : main.c
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@
	
stm32f411_startup.o : stm32f411_startup.c
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@
	
blink_led.elf : main.o stm32f411_startup.o
	$(CC) $(LDFLAGS) $^ -o $@

openocd:
	openocd -f board/st_nucleo_f4.cfg

clean:
	rm -f *.o *.elf *.map