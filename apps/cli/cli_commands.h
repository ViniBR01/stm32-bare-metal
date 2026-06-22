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

/**
 * @brief (Re)register the application's UART RX / TX-complete callbacks.
 *
 * The UART driver clears its callback pointers in uart_deinit().  Any code
 * path that tears the UART down and re-inits it (e.g. the stop_mode wake
 * sequence) must call this afterwards so the CLI keeps receiving input and
 * printf_dma keeps resetting its buffers.  Defined in cli_simple.c, where the
 * concrete callbacks live; declared here so cli_commands.c can reuse it.
 */
void cli_app_attach_uart_callbacks(void);

#ifdef HIL_TEST_MODE
/**
 * @brief Run all Unity tests on the target hardware
 * 
 * Executes the Unity test suite defined in test_harness.c.
 * Only available when compiled with HIL_TEST=1.
 * 
 * @return Unity exit code (0 = all tests passed)
 */
int run_unity_tests(void);
#endif

#endif /* CLI_COMMANDS_H */

