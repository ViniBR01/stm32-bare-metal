#include "led2.h"
#include "uart_echo.h"

int main(void) {
    led2_init();
    uart_echo_init();

    while (1) {
        led2_toggle();
        char c = uart_echo_read();
        switch(c) {
            case '\b':
            case 127: // Handle DEL as backspace
                uart_echo_write('\b');
                uart_echo_write(' ');
                uart_echo_write('\b');
                continue;
            case '\r':
                c = '\n';
                break;
            default:
                break;
        }
        uart_echo_write(c);
    }
}
