#include "led2.h"
#include "uart_echo.h"

#define MAX_CMD_SIZE 32

static uint8_t buf[MAX_CMD_SIZE];
static uint32_t buf_size = 0;

void send_command() {
    for (uint32_t i = 0; i < buf_size; i++) {
        uart_echo_write(buf[i]);
    }
    if (buf_size > 0) {
        uart_echo_write('\n');
        buf_size = 0; // flush the buffer!
    }
}

void handle_input(char c) {
    switch(c) {
        case '\b':
        case 127: // Handle DEL as backspace
            uart_echo_write('\b');
            uart_echo_write(' ');
            uart_echo_write('\b');
            buf_size = (buf_size > 0) ? buf_size - 1 : 0;
            break;
        case '\r': // Handle carriage return as newline
        case '\n':
            c = '\n';
            uart_echo_write(c);
            send_command();
            break;
        default:
            /* Check if the character is a printable ascii */
            if (c >= 32 && buf_size < sizeof(buf) - 1) {
                buf[buf_size++] = c;
                uart_echo_write(c);
            }
            break;
    }
}

int main(void) {
    buf_size = 0;
    led2_init();
    uart_echo_init();

    while (1) {
        led2_toggle();
        char c = uart_echo_read();
        handle_input(c);
    }
}
