#ifndef CLI_COMMANDS_H
#define CLI_COMMANDS_H

#include "cli.h"

/**
 * @brief Get the array of application-specific CLI commands
 * 
 * Returns a pointer to the command table and sets the number of commands.
 * The command table includes LED control commands (led_on, led_off, led_toggle).
 * 
 * @param num_commands Output parameter for the number of commands in the table
 * @return Pointer to the command array (const, do not modify)
 */
const cli_command_t* cli_commands_get_table(size_t* num_commands);

#endif /* CLI_COMMANDS_H */

