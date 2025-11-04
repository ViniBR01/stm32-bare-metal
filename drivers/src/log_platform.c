/**
 * @file log_platform.c
 * @brief Platform-specific logging integration implementation
 * 
 * This file implements the singleton pattern for managing the logging backend
 * on STM32 platforms. It provides the bridge between log_c library and the
 * hardware UART interface.
 * 
 * Design Notes:
 * - Singleton pattern used for state management (future-ready for runtime config)
 * - Static allocation only (no malloc/free)
 * - Interrupt-safe by design (uses blocking UART writes)
 * - Overrides weak _putchar() symbol from log_c library
 */

#include "log_platform.h"
#include "uart.h"
#include <stddef.h>

/**
 * @brief Singleton context for logging platform state
 * 
 * This structure holds the internal state of the logging platform.
 * Currently minimal, but designed to be extended in the future with:
 * - Runtime log level configuration
 * - Statistics (messages logged, errors, etc.)
 * - Buffering configuration
 * - Multiple backend support
 */
typedef struct {
    bool initialized;              /**< True if logging has been initialized */
    void (*backend_putchar)(char); /**< Function pointer to backend putchar */
} log_platform_context_t;

/**
 * @brief Global singleton instance (static storage)
 * 
 * Zero-initialized at startup. The initialized flag starts as false,
 * and backend_putchar starts as NULL.
 */
static log_platform_context_t g_log_ctx = {0};

/**
 * @brief UART backend putchar implementation
 * 
 * This is the default backend that writes characters to UART2.
 * It's interrupt-safe because uart_write() is blocking and atomic.
 * 
 * @param character Character to output
 */
static void uart_backend_putchar(char character) {
    uart_write(character);
}

/**
 * @brief Implementation of _putchar required by printf library
 * 
 * This function overrides the weak _putchar() symbol defined in log_c.
 * It routes all character output through the configured backend.
 * 
 * Design considerations:
 * - Must be interrupt-safe (uses atomic UART operations)
 * - No dynamic memory allocation
 * - Falls back to doing nothing if not initialized (safe failure mode)
 * 
 * @param character Character to output
 */
void _putchar(char character) {
    /* Check if logging is initialized and backend is configured */
    if (g_log_ctx.initialized && g_log_ctx.backend_putchar != NULL) {
        /* Call the configured backend (UART or custom) */
        g_log_ctx.backend_putchar(character);
    }
    /* If not initialized, silently discard the character (safe failure) */
}

void log_platform_init_uart(void) {
    /* Idempotent: Don't re-initialize if already done */
    if (g_log_ctx.initialized) {
        return;
    }
    
    /* Initialize UART hardware (115200 baud, 8N1) */
    uart_init();
    
    /* Configure UART as the logging backend */
    g_log_ctx.backend_putchar = uart_backend_putchar;
    
    /* Mark as initialized */
    g_log_ctx.initialized = true;
}

void log_platform_init_custom(void (*putchar_fn)(char)) {
    /* Validate input parameter */
    if (putchar_fn == NULL) {
        /* Invalid parameter - do nothing (safe failure) */
        return;
    }
    
    /* Idempotent: Don't re-initialize if already done */
    if (g_log_ctx.initialized) {
        return;
    }
    
    /* Configure custom function as the logging backend */
    g_log_ctx.backend_putchar = putchar_fn;
    
    /* Mark as initialized */
    g_log_ctx.initialized = true;
    
    /* Note: We don't call uart_init() here - the user is responsible
     * for initializing whatever hardware their custom function uses */
}

bool log_platform_is_initialized(void) {
    return g_log_ctx.initialized;
}

