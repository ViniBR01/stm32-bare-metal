#include <printf.h>

#include "led2.h"
#include "systick.h"
#include "uart_terminal.h"

int main(void) {
    uart_terminal_init();
    led2_init();
    printf("Hello, UART Terminal!\n");

    uint32_t count = 0;
    while (1) {
        led2_off();
        systick_delay_ms(200);
        led2_on();
        systick_delay_ms(800);
        if (++count < 10) {
            printf("Tick... count=%lu\n", count);
        } else if (count % 10 == 0) {
            printf("Tick... count=%lu\n", count);
        }
    }
}
