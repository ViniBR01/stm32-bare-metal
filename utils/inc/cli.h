#ifndef CLI_H
#define CLI_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Command definition structure
 * 
 * Defines a CLI command with its name, description, and handler function.
 */
typedef struct {
    const char* name;           /**< Command name (null-terminated string) */
    const char* description;    /**< Brief description for help text */
    int (*handler)(const char* args); /**< Command handler function (returns 0 on success, non-zero on error) */
} cli_command_t;

/**
 * @brief Maximum number of commands supported (including built-in help command)
 */
#define CLI_MAX_COMMANDS 32

/**
 * @brief Maximum command size for history entries
 */
#define CLI_MAX_CMD_SIZE 64

/**
 * @brief Number of history entries to remember
 */
#define CLI_HISTORY_SIZE 8

/**
 * @brief CLI context structure
 * 
 * Maintains the state of the CLI including registered commands, input buffer,
 * and command history for recall via up/down arrow keys.
 */
typedef struct {
    cli_command_t command_list[CLI_MAX_COMMANDS]; /**< Internal array storing all commands (user + built-in) */
    size_t num_commands;                          /**< Total number of registered commands */
    char* buffer;                                 /**< Input buffer for command text */
    size_t buffer_size;                           /**< Maximum size of the buffer */
    size_t buffer_pos;                            /**< Current position in buffer */
    /* Command history (circular buffer) */
    char   history[CLI_HISTORY_SIZE][CLI_MAX_CMD_SIZE]; /**< Ring buffer of past commands */
    size_t history_count;                         /**< Number of entries stored (0..CLI_HISTORY_SIZE) */
    size_t history_head;                          /**< Index of next write slot (circular) */
    int    history_browse;                        /**< Current browse offset while navigating (-1 = not browsing) */
    char   history_stash[CLI_MAX_CMD_SIZE];       /**< Stash of in-progress input when browsing history */
    size_t history_stash_len;                     /**< Length of stashed input */
    /* ANSI escape sequence state machine */
    uint8_t esc_state;                            /**< 0=idle, 1=got ESC, 2=got ESC+[ */
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
 * Handles character input including printable characters, backspace, TAB, newline,
 * and ANSI escape sequences for arrow keys (Up/Down for command history).
 * TAB key triggers auto-completion: finds the longest common prefix among all
 * commands matching the current input and auto-completes to that point.
 * Up/Down arrow keys navigate through command history.
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

/**
 * @brief Execute the command currently in the buffer
 * 
 * This function can be called from main loop to execute commands in non-ISR context.
 * 
 * @param ctx Pointer to the CLI context structure
 */
void cli_execute_command(cli_context_t* ctx);

/**
 * @brief Save the current buffer contents to the command history
 * 
 * Should be called after Enter is pressed but before the buffer is reset.
 * Saves non-empty commands to the history ring buffer, skipping consecutive
 * duplicates.
 * 
 * @param ctx Pointer to the CLI context structure
 */
void cli_history_save(cli_context_t* ctx);

#endif /* CLI_H */

