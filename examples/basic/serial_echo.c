#include <stddef.h>

#include "led2.h"
#include "printf.h"
#include "string_utils.h"
#include "uart_echo.h"
#include "uart_terminal.h"

#define MAX_CMD_SIZE 32

// Command definition structure
typedef struct {
    const char* name;           // Command name
    const char* description;    // Brief description for help
    void (*handler)(void);      // Command handler function
} cli_command_t;

// CLI context structure
typedef struct {
    const cli_command_t* commands;  // Array of registered commands
    size_t num_commands;            // Number of registered commands
    char buffer[MAX_CMD_SIZE];      // Input buffer
    size_t buffer_pos;              // Current position in buffer
} cli_context_t;

// Global CLI context
cli_context_t g_cli;

// Forward declarations
static void cmd_led_on(void);
static void cmd_led_off(void);
static void cmd_led_toggle(void);
static void cmd_help(void);

// CLI function declarations
void cli_init(cli_context_t* ctx, const cli_command_t* commands, size_t num_commands);
void cli_process_char(cli_context_t* ctx, char c);
void cli_print_help(const cli_context_t* ctx);

// Command implementations
static void cmd_led_on(void) {
    led2_on();
    printf("LED2 turned on\n");
}

static void cmd_led_off(void) {
    led2_off();
    printf("LED2 turned off\n");
}

static void cmd_led_toggle(void) {
    led2_toggle();
    printf("LED2 toggled\n");
}

static void cmd_help(void) {
    cli_print_help(&g_cli);
}

// Command table
static const cli_command_t commands[] = {
    {"led_on",     "Turn on LED2",      cmd_led_on},
    {"led_off",    "Turn off LED2",     cmd_led_off},
    {"led_toggle", "Toggle LED2 state", cmd_led_toggle},
    {"help",       "Show this help",    cmd_help},
};

// CLI implementation functions
void cli_init(cli_context_t* ctx, const cli_command_t* commands, size_t num_commands) {
    ctx->commands = commands;
    ctx->num_commands = num_commands;
    ctx->buffer_pos = 0;
    // Clear the buffer
    for (size_t i = 0; i < MAX_CMD_SIZE; i++) {
        ctx->buffer[i] = '\0';
    }
}

void cli_print_help(const cli_context_t* ctx) {
    printf("\nAvailable commands:\n");
    for (size_t i = 0; i < ctx->num_commands; i++) {
        printf("%-12s - %s\n", ctx->commands[i].name, ctx->commands[i].description);
    }
}

static void cli_execute_command(cli_context_t* ctx) {
    // Null-terminate the buffer
    ctx->buffer[ctx->buffer_pos] = '\0';
    
    // Search for matching command
    for (size_t i = 0; i < ctx->num_commands; i++) {
        if (strlen(ctx->commands[i].name) == ctx->buffer_pos &&
            strncmp(ctx->buffer, ctx->commands[i].name, ctx->buffer_pos) == 0) {
            // Found matching command
            ctx->commands[i].handler();
            return;
        }
    }
    
    // No matching command found
    if (ctx->buffer_pos > 0) {
        printf("Unknown command: %s\n", ctx->buffer);
    }
}

void cli_process_char(cli_context_t* ctx, char c) {
    switch(c) {
        case '\b':
        case 127: // Handle DEL as backspace
            if (ctx->buffer_pos > 0) {
                printf("\b \b");
                ctx->buffer_pos--;
            }
            break;
            
        case '\r': // Handle carriage return as newline
        case '\n':
            uart_echo_write('\n');
            cli_execute_command(ctx);
            ctx->buffer_pos = 0; // Reset buffer
            break;
            
        default:
            // Check if the character is a printable ascii
            if (c >= 32 && c <= 126 && ctx->buffer_pos < MAX_CMD_SIZE - 1) {
                ctx->buffer[ctx->buffer_pos++] = c;
                uart_echo_write(c);
            }
            break;
    }
}

int main(void) {
    // Initialize hardware
    led2_init();
    uart_echo_init();
    uart_terminal_init(); // Initialize UART for printf output
    
    // Initialize CLI
    cli_init(&g_cli, commands, sizeof(commands)/sizeof(commands[0]));
    
    // Print welcome message and help
    printf("\n=== STM32 CLI Example ===\n");
    cli_print_help(&g_cli);
    printf("\n> ");

    while (1) {
        char c = uart_echo_read();
        cli_process_char(&g_cli, c);
        
        // Print prompt after command execution
        if (c == '\n' || c == '\r') {
            printf("> ");
        }
    }
}
