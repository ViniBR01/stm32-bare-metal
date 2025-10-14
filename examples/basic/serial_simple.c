#include "led2.h"
#include "log_c.h"
#include "systick.h"
#include "uart_terminal.h"

int main(void) {
    uart_terminal_init();
    logc_set_putchar(uart_write);
    led2_init();
    loginfo("Hello, UART Terminal!\n");

    uint32_t count = 0;
    while (1) {
        led2_off();
        systick_delay_ms(200);
        led2_on();
        systick_delay_ms(800);
        if (++count < 10) {
            loginfo("Tick... count=%lu", count);
        } else if (count % 10 == 0) {
            loginfo("Tick... count=%lu", count);
        }
    }
}
