#include <stddef.h>
#include "string_utils.h"

#include "led2.h"
#include "uart_echo.h"

#define MAX_CMD_SIZE 32

typedef struct {
    char data[MAX_CMD_SIZE];
    uint8_t size;
    void (*execute)(void);
} command_t;

typedef struct {
    uint8_t op_name[MAX_CMD_SIZE];
    void (*op_callback)();
} ops_t;

typedef struct cli_t_ cli_t;
struct cli_t_ {
    ops_t operations[5];
    uint32_t num_operations;
    command_t (*get_command)(cli_t* cli, uint8_t* buf, uint32_t buf_size);
};


static uint8_t buf[MAX_CMD_SIZE];
static uint32_t buf_size = 0;
cli_t my_cli;

void command_invoker(cli_t* CLI) {
    // Build command from buffer and send to receiver
    if (buf_size > 0) {
        command_t cmd = CLI->get_command(CLI, buf, buf_size);
        if (cmd.size > 0 && cmd.execute != NULL) {
            cmd.execute();
        } else {
            uart_echo_write('N');
            uart_echo_write('/');
            uart_echo_write('A');
            uart_echo_write('\n');
        }

        buf_size = 0; // flush the buffer
    }
}

//TODO: implement history in cli, with navigation via arrow keys (up/down)
//TODO: implement tab autocomplete based on the current commands available in cli
void handle_input(char c, cli_t* CLI) {
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
            command_invoker(CLI);
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

command_t get_command_led(cli_t* cli, uint8_t* buf, uint32_t buf_size) {
    command_t cmd;
    for (uint32_t i = 0; i < cli->num_operations; i++) {
        if (buf_size == strlen((char*)cli->operations[i].op_name) &&
            strncmp((char*)buf, (char*)cli->operations[i].op_name, buf_size) == 0) {
            // Match found
            cmd.size = buf_size;
            memcpy(cmd.data, buf, buf_size);
            // Assign the callback function to the command (if needed)
            cmd.execute = cli->operations[i].op_callback;
            return cmd;
        } else {
            cmd.size = 0;
            cmd.execute = NULL;
        }
    }
    return cmd;
}

// TODO: generate the help message automatically from the commands registered in the CLI
void print_help(void) {
    const char* help_msg = "\nAvailable commands:\n"
                           "set_led     - Turn on LED2\n"
                           "reset_led   - Turn off LED2\n"
                           "toggle_led  - Toggle LED2 state\n"
                           "help        - Show this help message\n";
    for (size_t i = 0; i < strlen(help_msg); i++) {
        uart_echo_write(help_msg[i]);
    }
}

cli_t* cli_init(void) {
    my_cli.get_command = get_command_led;
    // Create commands and store in my_cli.operations
    // TODO: refactor to allow dynamic registration of commands
    strcpy((char*)my_cli.operations[0].op_name, "set_led");
    my_cli.operations[0].op_callback = led2_on; // set_led_callback;
    strcpy((char*)my_cli.operations[1].op_name, "reset_led");
    my_cli.operations[1].op_callback = led2_off; // reset_led_callback;
    strcpy((char*)my_cli.operations[2].op_name, "toggle_led");
    my_cli.operations[2].op_callback = led2_toggle; // toggle_led_callback;
    strcpy((char*)my_cli.operations[3].op_name, "help");
    my_cli.operations[3].op_callback = print_help; // help_callback;
    my_cli.num_operations = 4;
    return &my_cli;
}

int main(void) {
    buf_size = 0; // Initialize buffer size
    led2_init();
    uart_echo_init();
    cli_t* CLI = cli_init();

    // CLI->print_welcome_message(); // TODO!
    print_help();

    while (1) {
        char c = uart_echo_read();
        handle_input(c, CLI);
    }
}
