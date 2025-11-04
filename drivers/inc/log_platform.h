#ifndef LOG_PLATFORM_H_
#define LOG_PLATFORM_H_

#include <stdbool.h>

/**
 * @file log_platform.h
 * @brief Platform-specific logging integration layer for STM32
 * 
 * This module provides a clean abstraction layer between the log_c library
 * and the STM32 hardware. It implements a singleton pattern to manage logging
 * backend configuration and provides a simple initialization API.
 * 
 * Key Features:
 * - Single-function initialization (no manual _putchar() implementation needed)
 * - Thread and interrupt safe by design
 * - Supports UART backend (default) and custom backends
 * - Future-ready architecture for runtime configuration
 * - Zero dynamic memory allocation
 * 
 * Thread Safety:
 * All functions in this module are designed to be interrupt-safe. The _putchar()
 * implementation uses blocking UART writes which are inherently atomic. The
 * singleton state is read-only after initialization, making it safe to call
 * logging functions from both main loop and interrupt contexts.
 * 
 * Usage Example:
 * @code
 * #include "log_platform.h"
 * #include "log_c.h"
 * 
 * int main(void) {
 *     // Initialize logging with UART backend
 *     log_platform_init_uart();
 *     
 *     // Use log_c macros directly
 *     loginfo("System initialized");
 *     logdebug("Debug message");
 *     
 *     // Safe to call from interrupts too
 * }
 * @endcode
 */

/**
 * @brief Initialize logging with UART backend
 * 
 * This function initializes the logging subsystem to use UART2 as the output
 * backend. It performs the following operations:
 * 1. Calls uart_init() to set up UART2 hardware
 * 2. Configures the log_c library to use UART via _putchar()
 * 3. Marks the logging system as initialized
 * 
 * This function is idempotent - calling it multiple times is safe and will
 * only initialize once.
 * 
 * @note UART is configured to 115200 baud, 8N1 format
 * @note This function must be called before any log macros (loginfo, etc.)
 * 
 * Thread Safety: Safe to call from main context only (not from interrupts)
 */
void log_platform_init_uart(void);

/**
 * @brief Initialize logging with a custom putchar backend
 * 
 * This function allows advanced users to provide their own character output
 * function. This is useful for:
 * - Redirecting logs to a different UART
 * - Buffering log output
 * - Sending logs over SPI, I2C, or other interfaces
 * - Writing logs to flash memory
 * 
 * The provided putchar_fn will be called for every character in log messages.
 * It must be reentrant and interrupt-safe if you plan to log from ISRs.
 * 
 * Example:
 * @code
 * void my_putchar(char c) {
 *     // Custom implementation
 *     spi_write_byte(c);
 * }
 * 
 * int main(void) {
 *     log_platform_init_custom(my_putchar);
 *     loginfo("This will go to SPI");
 * }
 * @endcode
 * 
 * @param putchar_fn Pointer to a function that outputs a single character
 *                   Must not be NULL
 * 
 * Thread Safety: Safe to call from main context only (not from interrupts)
 */
void log_platform_init_custom(void (*putchar_fn)(char));

/**
 * @brief Check if logging has been initialized
 * 
 * @return true if log_platform_init_uart() or log_platform_init_custom()
 *         has been called, false otherwise
 * 
 * Thread Safety: Safe to call from any context (main or interrupt)
 */
bool log_platform_is_initialized(void);

#endif /* LOG_PLATFORM_H_ */

