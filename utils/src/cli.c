#include "cli.h"
#include "printf.h"
#include "string_utils.h"

// Global pointer to current CLI context (needed for built-in help command)
static cli_context_t* g_current_cli_ctx = NULL;

/**
 * @brief Built-in help command handler
 * 
 * @return 0 on success
 */
static int cli_builtin_help_handler(void) {
    if (g_current_cli_ctx) {
        cli_print_help(g_current_cli_ctx);
    }
    return 0;
}

/**
 * @brief Execute the command currently in the buffer
 * 
 * @param ctx Pointer to the CLI context structure
 */
static void cli_execute_command(cli_context_t* ctx) {
    // Null-terminate the buffer
    ctx->buffer[ctx->buffer_pos] = '\0';
    
    // Search for matching command
    for (size_t i = 0; i < ctx->num_commands; i++) {
        if (strlen(ctx->command_list[i].name) == ctx->buffer_pos &&
            strncmp(ctx->buffer, ctx->command_list[i].name, ctx->buffer_pos) == 0) {
            // Found matching command - execute it and check return code
            int result = ctx->command_list[i].handler();
            
            if (result == 0) {
                printf("[OK]\n");
            } else {
                printf("[ERROR] Command '%s' failed with error code: %d\n", 
                       ctx->command_list[i].name, result);
            }
            return;
        }
    }
    
    // No matching command found
    if (ctx->buffer_pos > 0) {
        printf("[ERROR] Unknown command: %s\n", ctx->buffer);
    }
}

void cli_init(cli_context_t* ctx, const cli_command_t* commands, size_t num_commands,
              char* buffer, size_t buffer_size) {
    // Set global context pointer for built-in commands
    g_current_cli_ctx = ctx;
    
    // Initialize buffer
    ctx->buffer = buffer;
    ctx->buffer_size = buffer_size;
    ctx->buffer_pos = 0;
    
    // Clear the input buffer
    for (size_t i = 0; i < buffer_size; i++) {
        ctx->buffer[i] = '\0';
    }
    
    // Check if we have room for user commands + built-in help command
    if (num_commands >= CLI_MAX_COMMANDS) {
        printf("[ERROR] Too many commands! Maximum is %d (including built-in help)\n", 
               CLI_MAX_COMMANDS - 1);
        ctx->num_commands = 0;
        return;
    }
    
    // Copy user commands to internal command list
    for (size_t i = 0; i < num_commands; i++) {
        ctx->command_list[i] = commands[i];
    }
    
    // Add built-in help command as the last command
    ctx->command_list[num_commands].name = "help";
    ctx->command_list[num_commands].description = "Show this help message";
    ctx->command_list[num_commands].handler = cli_builtin_help_handler;
    
    // Set total number of commands (user commands + help)
    ctx->num_commands = num_commands + 1;
}

void cli_print_help(const cli_context_t* ctx) {
    printf("\nAvailable commands:\n");
    for (size_t i = 0; i < ctx->num_commands; i++) {
        printf("%-12s - %s\n", ctx->command_list[i].name, ctx->command_list[i].description);
    }
}

void cli_print_welcome(const char* message) {
    if (message) {
        printf("%s\n", message);
    }
    printf("Type 'help' to see the list of available commands\n");
}

void cli_process_char(cli_context_t* ctx, char c, void (*echo_fn)(char)) {
    switch(c) {
        case '\b':
        case 127: // Handle DEL as backspace
            if (ctx->buffer_pos > 0) {
                if (echo_fn) {
                    // Echo backspace sequence: backspace, space, backspace
                    echo_fn('\b');
                    echo_fn(' ');
                    echo_fn('\b');
                }
                ctx->buffer_pos--;
            }
            break;
            
        case '\r': // Handle carriage return as newline
        case '\n':
            if (echo_fn) {
                echo_fn('\n');
            }
            cli_execute_command(ctx);
            ctx->buffer_pos = 0; // Reset buffer
            break;
            
        default:
            // Check if the character is a printable ascii
            if (c >= 32 && c <= 126 && ctx->buffer_pos < ctx->buffer_size - 1) {
                ctx->buffer[ctx->buffer_pos++] = c;
                if (echo_fn) {
                    echo_fn(c);
                }
            }
            break;
    }
}

