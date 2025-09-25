CC = arm-none-eabi-gcc
CFLAGS = -c -mcpu=cortex-m4 -mthumb -std=gnu11
LDFLAGS = -nostdlib -T stm32_ls.ld -Wl,-Map=blink_led.map

all: blink_led.elf 

main.o : main.c
	$(CC) $(CFLAGS) $^ -o $@
	
stm32f411_startup.o : stm32f411_startup.c
	$(CC) $(CFLAGS) $^ -o $@
	
blink_led.elf : main.o stm32f411_startup.o
	$(CC) $(LDFLAGS) $^ -o $@

openocd:
	openocd -f board/st_nucleo_f4.cfg

clean:
	rm -f *.o *.elf *.map