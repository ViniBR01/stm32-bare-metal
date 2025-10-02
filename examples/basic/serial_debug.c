#include <stdio.h>

#include "uart_terminal.h"
#include "systick.h"

int main(void) {
    uart_terminal_init();

    printf("Hello, UART Terminal!\n");

    while (1) {
        systick_delay_ms(1000);
        printf("Tick...\n");
    }
}
