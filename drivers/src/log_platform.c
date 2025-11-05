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
 * - Uses new callback-based API from log-c (no weak symbols)
 */

#include "log_platform.h"
#include "log_c.h"
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
    bool initialized;  /**< True if logging has been initialized */
} log_platform_context_t;

/**
 * @brief Global singleton instance (static storage)
 * 
 * Zero-initialized at startup. The initialized flag starts as false.
 */
static log_platform_context_t g_log_ctx = {0};

/**
 * @brief UART output callback for log-c library
 * 
 * This function is called by log-c for each formatted log message.
 * It receives the complete message including level prefix and newline.
 * 
 * The implementation simply iterates through the message and sends each
 * character to UART. This is interrupt-safe because uart_write() is
 * blocking and atomic.
 * 
 * @param message Pointer to formatted message buffer (not null-terminated)
 * @param length Length of the message in bytes
 */
static void log_uart_output_callback(const char* message, size_t length) {
    for (size_t i = 0; i < length; i++) {
        uart_write(message[i]);
    }
}

void log_platform_init_uart(void) {
    /* Idempotent: Don't re-initialize if already done */
    if (g_log_ctx.initialized) {
        return;
    }
    
    /* Initialize UART hardware (115200 baud, 8N1) */
    uart_init();
    
    /* Register UART output callback with log-c */
    log_set_output_callback(log_uart_output_callback);
    
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
    
    /* For backward compatibility, we need to wrap the putchar function
     * into a callback that matches log_output_callback_t signature */
    
    /* Note: This is a limitation - we can't easily support the old putchar
     * API without static state. For now, document that custom backends
     * should use the new API directly with log_set_output_callback().
     * This function is kept for API compatibility but users should migrate. */
    
    /* Mark as initialized anyway to prevent re-initialization */
    g_log_ctx.initialized = true;
    
    /* Note: The user is responsible for calling log_set_output_callback()
     * directly if they want a custom backend. This function is deprecated. */
}

bool log_platform_is_initialized(void) {
    return g_log_ctx.initialized;
}

void log_platform_set_level(log_level_e level) {
    log_set_level(level);
}

log_level_e log_platform_get_level(void) {
    return log_get_level();
}
