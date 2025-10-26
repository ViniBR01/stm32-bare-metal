#include <stddef.h>

#include "cli.h"
#include "led2.h"
#include "printf.h"
#include "uart.h"

#define MAX_CMD_SIZE 32

// Global CLI context and buffer
cli_context_t g_cli;
char g_cmd_buffer[MAX_CMD_SIZE];

// Forward declarations
static int cmd_led_on(void);
static int cmd_led_off(void);
static int cmd_led_toggle(void);

// Command implementations
static int cmd_led_on(void) {
    led2_on();
    printf("LED2 turned on\n");
    return 0;  // Success
}

static int cmd_led_off(void) {
    led2_off();
    printf("LED2 turned off\n");
    return 0;  // Success
}

static int cmd_led_toggle(void) {
    led2_toggle();
    printf("LED2 toggled\n");
    return 0;  // Success
}

// Command table (help command is automatically added by CLI library)
static const cli_command_t commands[] = {
    {"led_on",     "Turn on LED2",      cmd_led_on},
    {"led_off",    "Turn off LED2",     cmd_led_off},
    {"led_toggle", "Toggle LED2 state", cmd_led_toggle},
};

void _putchar(char character) {
    uart_write(character);
}

int main(void) {
    // Initialize hardware
    led2_init();
    uart_init();
    
    // Initialize CLI
    cli_init(&g_cli, commands, sizeof(commands)/sizeof(commands[0]), 
             g_cmd_buffer, MAX_CMD_SIZE);
    
    // Print welcome message
    cli_print_welcome("\n=== STM32 CLI Example ===");
    printf("\n> ");

    while (1) {
        char c = uart_read();
        cli_process_char(&g_cli, c, uart_write);
        
        // Print prompt after command execution
        if (c == '\n' || c == '\r') {
            printf("> ");
        }
    }
}
