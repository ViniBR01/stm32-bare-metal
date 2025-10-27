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
void cli_execute_command(cli_context_t* ctx) {
    // Null-terminate the buffer
    ctx->buffer[ctx->buffer_pos] = '\0';
    
    // Search for matching command
    for (size_t i = 0; i < ctx->num_commands; i++) {
        if (strlen(ctx->command_list[i].name) == ctx->buffer_pos &&
            strncmp(ctx->buffer, ctx->command_list[i].name, ctx->buffer_pos) == 0) {
            // Found matching command - execute it
            ctx->command_list[i].handler();
            return;
        }
    }
    
    // No matching command found
    if (ctx->buffer_pos > 0) {
        printf("Unknown command: %s\n", ctx->buffer);
    }
}

/**
 * @brief Find the longest common prefix among commands matching the current buffer
 * 
 * This function searches through all registered commands to find those that start
 * with the current buffer content, then calculates the longest common prefix
 * among all matching commands.
 * 
 * @param ctx Pointer to the CLI context structure
 * @return Length of the longest common prefix (0 if no matches or no extension possible)
 */
static size_t cli_find_common_prefix(cli_context_t* ctx) {
    // If buffer is empty, no meaningful completion
    if (ctx->buffer_pos == 0) {
        return 0;
    }
    
    // Null-terminate current buffer for string comparison
    ctx->buffer[ctx->buffer_pos] = '\0';
    
    // Find first matching command to establish baseline
    const char* first_match = NULL;
    size_t first_match_len = 0;
    
    for (size_t i = 0; i < ctx->num_commands; i++) {
        if (strncmp(ctx->buffer, ctx->command_list[i].name, ctx->buffer_pos) == 0) {
            first_match = ctx->command_list[i].name;
            first_match_len = strlen(first_match);
            break;
        }
    }
    
    // No matches found
    if (first_match == NULL) {
        return 0;
    }
    
    // Find common prefix length across all matching commands
    size_t common_prefix_len = first_match_len;
    
    for (size_t i = 0; i < ctx->num_commands; i++) {
        const char* cmd_name = ctx->command_list[i].name;
        
        // Skip commands that don't match the current prefix
        if (strncmp(ctx->buffer, cmd_name, ctx->buffer_pos) != 0) {
            continue;
        }
        
        // Find how much of this command matches the common prefix so far
        size_t match_len = 0;
        while (match_len < common_prefix_len && 
               match_len < strlen(cmd_name) &&
               first_match[match_len] == cmd_name[match_len]) {
            match_len++;
        }
        
        // Update common prefix length to the shorter match
        if (match_len < common_prefix_len) {
            common_prefix_len = match_len;
        }
    }
    
    return common_prefix_len;
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
            
        case '\t': // TAB key - auto-complete
            {
                // Find the longest common prefix among matching commands
                size_t common_prefix_len = cli_find_common_prefix(ctx);
                
                // Only auto-complete if we have a valid extension and room in buffer
                if (common_prefix_len > ctx->buffer_pos && 
                    common_prefix_len < ctx->buffer_size) {
                    
                    // Find first matching command to copy the prefix from
                    for (size_t i = 0; i < ctx->num_commands; i++) {
                        if (strncmp(ctx->buffer, ctx->command_list[i].name, ctx->buffer_pos) == 0) {
                            // Copy new characters to buffer and echo them
                            for (size_t j = ctx->buffer_pos; j < common_prefix_len; j++) {
                                ctx->buffer[j] = ctx->command_list[i].name[j];
                                if (echo_fn) {
                                    echo_fn(ctx->buffer[j]);
                                }
                            }
                            // Update buffer position
                            ctx->buffer_pos = common_prefix_len;
                            break;
                        }
                    }
                }
            }
            break;
            
        case '\r': // Handle carriage return as newline
        case '\n':
            if (echo_fn) {
                echo_fn('\n');
            }
            // Note: Command execution is not triggered here anymore.
            // The caller should check if Enter was pressed and call
            // cli_execute_command() from main loop context.
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

