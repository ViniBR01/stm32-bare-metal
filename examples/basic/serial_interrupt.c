#include <stdio.h>

#include "led2.h"
#include "uart_terminal.h"
#include "systick.h"

int main(void) {
    uart_terminal_init();
    led2_init();
    printf("Hello, UART Terminal!\n");

    while (1) {
        led2_off();
        systick_delay_ms(200);
        led2_on();
        systick_delay_ms(800);
        printf("Tick...\n");
    }
}
