#ifndef CLI_H
#define CLI_H

#include <stddef.h>

/**
 * @brief Command definition structure
 * 
 * Defines a CLI command with its name, description, and handler function.
 */
typedef struct {
    const char* name;           /**< Command name (null-terminated string) */
    const char* description;    /**< Brief description for help text */
    int (*handler)(void);       /**< Command handler function (returns 0 on success, non-zero on error) */
} cli_command_t;

/**
 * @brief Maximum number of commands supported (including built-in help command)
 */
#define CLI_MAX_COMMANDS 32

/**
 * @brief CLI context structure
 * 
 * Maintains the state of the CLI including registered commands and input buffer.
 */
typedef struct {
    cli_command_t command_list[CLI_MAX_COMMANDS]; /**< Internal array storing all commands (user + built-in) */
    size_t num_commands;                          /**< Total number of registered commands */
    char* buffer;                                 /**< Input buffer for command text */
    size_t buffer_size;                           /**< Maximum size of the buffer */
    size_t buffer_pos;                            /**< Current position in buffer */
} cli_context_t;

/**
 * @brief Initialize the CLI context
 * 
 * @param ctx Pointer to the CLI context structure
 * @param commands Array of command definitions
 * @param num_commands Number of commands in the array
 * @param buffer Buffer for storing command input
 * @param buffer_size Size of the input buffer
 */
void cli_init(cli_context_t* ctx, const cli_command_t* commands, size_t num_commands,
              char* buffer, size_t buffer_size);

/**
 * @brief Process a single character of input
 * 
 * Handles character input including printable characters, backspace, and newline.
 * When a newline is received, the command is executed automatically.
 * 
 * @param ctx Pointer to the CLI context structure
 * @param c Character to process
 * @param echo_fn Function to echo characters back to the user (can be NULL for no echo)
 */
void cli_process_char(cli_context_t* ctx, char c, void (*echo_fn)(char));

/**
 * @brief Print help information for all registered commands
 * 
 * @param ctx Pointer to the CLI context structure
 */
void cli_print_help(const cli_context_t* ctx);

/**
 * @brief Print a welcome message with instructions
 * 
 * Prints the provided welcome message followed by an instruction to type 'help'
 * for available commands.
 * 
 * @param message Welcome message to display (can be NULL to skip custom message)
 */
void cli_print_welcome(const char* message);

#endif /* CLI_H */

