#include "led2.h"
#include "uart_echo.h"

#define MAX_CMD_SIZE 32

typedef struct {
    char data[MAX_CMD_SIZE];
    uint8_t size;
    // void (*execute)(void);
} command_t;

static uint8_t buf[MAX_CMD_SIZE];
static uint32_t buf_size = 0;

void echo_back(command_t* cmd) {
    // For now, echo back the command received
    for (uint32_t i = 0; i < cmd->size; i++) {
        uart_echo_write(cmd->data[i]);
    }
    if (cmd->size > 0) {
        uart_echo_write('\n');
    }
}

void command_invoker() {
    // Build command from buffer and send to receiver
    // TODO: should use the factory patter to crate different commands
    // based on the input received: cmd = factory.get_command(buf);
    // Then, execute the command: cmd->execute();
    // For now, just echo back the buffer as is
    if (buf_size > 0) {
        command_t cmd;
        for (uint32_t i = 0; i < buf_size; i++) {
            cmd.data[i] = buf[i];
        }
        cmd.size = buf_size;
        buf_size = 0; // flush the buffer!
        echo_back(&cmd);
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
            command_invoker();
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
