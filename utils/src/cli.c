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
static int cli_builtin_help_handler(const char* args) {
    (void)args;
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
        size_t cmd_len = strlen(ctx->command_list[i].name);
        
        // Command must match as prefix, followed by whitespace or end-of-string
        if (ctx->buffer_pos >= cmd_len &&
            strncmp(ctx->buffer, ctx->command_list[i].name, cmd_len) == 0 &&
            (ctx->buffer[cmd_len] == '\0' || ctx->buffer[cmd_len] == ' ')) {
            
            // Extract argument string: skip command name and leading whitespace
            const char* args = &ctx->buffer[cmd_len];
            while (*args == ' ') {
                args++;
            }
            
            // Found matching command - execute it with args
            ctx->command_list[i].handler(args);
            return;
        }
    }
    
    // No matching command found
    if (ctx->buffer_pos > 0) {
        printf("Unknown command: %s\n", ctx->buffer);
    }
}

void cli_history_save(cli_context_t* ctx) {
    // Null-terminate the buffer
    ctx->buffer[ctx->buffer_pos] = '\0';
    
    // Don't save empty commands
    if (ctx->buffer_pos == 0) {
        return;
    }
    
    // Cap to history entry size
    size_t len = ctx->buffer_pos;
    if (len >= CLI_MAX_CMD_SIZE) {
        len = CLI_MAX_CMD_SIZE - 1;
    }
    
    // Skip consecutive duplicates: compare with most recent entry
    if (ctx->history_count > 0) {
        size_t last = (ctx->history_head + CLI_HISTORY_SIZE - 1) % CLI_HISTORY_SIZE;
        if (strncmp(ctx->buffer, ctx->history[last], len) == 0 &&
            ctx->history[last][len] == '\0') {
            // Reset browse state and return without saving
            ctx->history_browse = -1;
            return;
        }
    }
    
    // Copy command into the ring buffer at the head position
    for (size_t i = 0; i < len; i++) {
        ctx->history[ctx->history_head][i] = ctx->buffer[i];
    }
    ctx->history[ctx->history_head][len] = '\0';
    
    // Advance head circularly
    ctx->history_head = (ctx->history_head + 1) % CLI_HISTORY_SIZE;
    
    // Increment count up to the maximum
    if (ctx->history_count < CLI_HISTORY_SIZE) {
        ctx->history_count++;
    }
    
    // Reset browse state
    ctx->history_browse = -1;
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
    
    // Initialize command history
    ctx->history_count = 0;
    ctx->history_head = 0;
    ctx->history_browse = -1;
    ctx->history_stash_len = 0;
    ctx->esc_state = 0;
    for (size_t i = 0; i < CLI_HISTORY_SIZE; i++) {
        ctx->history[i][0] = '\0';
    }
    ctx->history_stash[0] = '\0';
    
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

/**
 * @brief Clear the current line on the terminal and reprint the prompt
 * 
 * Sends \r, overwrites with spaces, then repositions cursor after prompt.
 */
static void cli_clear_line(size_t old_len, void (*echo_fn)(char)) {
    if (!echo_fn) {
        return;
    }
    // Move cursor to beginning of line
    echo_fn('\r');
    // Overwrite prompt + old content with spaces
    echo_fn(' '); echo_fn(' '); // "> " prompt
    for (size_t i = 0; i < old_len; i++) {
        echo_fn(' ');
    }
    // Return to beginning and reprint prompt
    echo_fn('\r');
    echo_fn('>'); echo_fn(' ');
}

/**
 * @brief Recall a history entry (or stash) and display it on the current line
 * 
 * Clears the current line, copies the source string into the buffer,
 * updates buffer_pos, and echoes the new content.
 */
static void cli_history_show(cli_context_t* ctx, const char* src, void (*echo_fn)(char)) {
    size_t old_len = ctx->buffer_pos;
    
    // Compute length of source string (capped to buffer_size - 1)
    size_t len = 0;
    while (src[len] != '\0' && len < ctx->buffer_size - 1) {
        len++;
    }
    
    // Clear the current line
    cli_clear_line(old_len, echo_fn);
    
    // Copy source into buffer and echo
    for (size_t i = 0; i < len; i++) {
        ctx->buffer[i] = src[i];
        if (echo_fn) {
            echo_fn(src[i]);
        }
    }
    ctx->buffer_pos = len;
}

/**
 * @brief Handle Up arrow key press - recall older history entry
 */
static void cli_history_up(cli_context_t* ctx, void (*echo_fn)(char)) {
    if (ctx->history_count == 0) {
        return;
    }
    
    // First Up press: stash current input
    if (ctx->history_browse == -1) {
        ctx->buffer[ctx->buffer_pos] = '\0';
        size_t len = ctx->buffer_pos;
        if (len >= CLI_MAX_CMD_SIZE) {
            len = CLI_MAX_CMD_SIZE - 1;
        }
        for (size_t i = 0; i < len; i++) {
            ctx->history_stash[i] = ctx->buffer[i];
        }
        ctx->history_stash[len] = '\0';
        ctx->history_stash_len = len;
        ctx->history_browse = 0;
    } else {
        // Already browsing - try to go further back
        if ((size_t)(ctx->history_browse + 1) >= ctx->history_count) {
            return; // Already at oldest entry
        }
        ctx->history_browse++;
    }
    
    // Compute the index into the ring buffer
    // history_head points to next write slot, so most recent is head-1
    // browse offset 0 = most recent, 1 = one before, etc.
    size_t idx = (ctx->history_head + CLI_HISTORY_SIZE - 1 - (size_t)ctx->history_browse) % CLI_HISTORY_SIZE;
    
    cli_history_show(ctx, ctx->history[idx], echo_fn);
}

/**
 * @brief Handle Down arrow key press - recall newer history entry or restore stash
 */
static void cli_history_down(cli_context_t* ctx, void (*echo_fn)(char)) {
    // Not browsing history - nothing to do
    if (ctx->history_browse == -1) {
        return;
    }
    
    ctx->history_browse--;
    
    if (ctx->history_browse < 0) {
        // Returned past newest entry - restore stashed input
        ctx->history_browse = -1;
        cli_history_show(ctx, ctx->history_stash, echo_fn);
        return;
    }
    
    // Show the newer history entry
    size_t idx = (ctx->history_head + CLI_HISTORY_SIZE - 1 - (size_t)ctx->history_browse) % CLI_HISTORY_SIZE;
    cli_history_show(ctx, ctx->history[idx], echo_fn);
}

void cli_process_char(cli_context_t* ctx, char c, void (*echo_fn)(char)) {
    // Handle ANSI escape sequence state machine (for arrow keys)
    if (ctx->esc_state == 1) {
        // Expecting '[' after ESC
        if (c == '[') {
            ctx->esc_state = 2;
        } else {
            ctx->esc_state = 0; // Invalid sequence, discard
        }
        return;
    }
    if (ctx->esc_state == 2) {
        // Expecting arrow key code after ESC [
        ctx->esc_state = 0;
        switch (c) {
            case 'A': // Up arrow
                cli_history_up(ctx, echo_fn);
                return;
            case 'B': // Down arrow
                cli_history_down(ctx, echo_fn);
                return;
            default:
                // Ignore other escape sequences (Left, Right, etc.)
                return;
        }
    }
    
    switch(c) {
        case 0x1B: // ESC - start of escape sequence
            ctx->esc_state = 1;
            break;
            
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
                size_t common_prefix_len = cli_find_common_prefix(ctx);
                
                if (common_prefix_len > ctx->buffer_pos && 
                    common_prefix_len < ctx->buffer_size) {
                    
                    for (size_t i = 0; i < ctx->num_commands; i++) {
                        if (strncmp(ctx->buffer, ctx->command_list[i].name, ctx->buffer_pos) == 0) {
                            for (size_t j = ctx->buffer_pos; j < common_prefix_len; j++) {
                                ctx->buffer[j] = ctx->command_list[i].name[j];
                                if (echo_fn) {
                                    echo_fn(ctx->buffer[j]);
                                }
                            }
                            ctx->buffer_pos = common_prefix_len;
                            break;
                        }
                    }

                    /* If the completed prefix is a single exact command name, append a space */
                    size_t exact_matches = 0;
                    ctx->buffer[ctx->buffer_pos] = '\0';
                    for (size_t i = 0; i < ctx->num_commands; i++) {
                        size_t cmd_len = strlen(ctx->command_list[i].name);
                        if (cmd_len == ctx->buffer_pos &&
                            strncmp(ctx->buffer, ctx->command_list[i].name, cmd_len) == 0) {
                            exact_matches++;
                        }
                    }
                    if (exact_matches == 1 && ctx->buffer_pos < ctx->buffer_size - 1) {
                        ctx->buffer[ctx->buffer_pos++] = ' ';
                        if (echo_fn) {
                            echo_fn(' ');
                        }
                    }
                }
            }
            break;
            
        case '\r':
        case '\n':
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

