#include "led2.h"
#include "log_c.h"
#include "log_platform.h"
#include "systick.h"

/**
 * @brief Serial simple example - demonstrates logging with UART backend
 * 
 * This example shows how to use the log_platform integration layer to easily
 * set up logging with UART output. The platform layer handles all the wiring
 * between log_c, printf, and the UART driver.
 * 
 * Key features demonstrated:
 * - Simple one-function initialization (log_platform_init_uart)
 * - No manual _putchar() implementation needed
 * - Safe to use from main loop (and interrupts, though not shown here)
 * - Structured logging with log levels (loginfo, logdebug, etc.)
 * 
 * The compile-time LOG_LEVEL controls which messages are included in the
 * binary. Build with different levels:
 *   make EXAMPLE=serial_simple LOG_LEVEL=LOG_LEVEL_DEBUG
 *   make EXAMPLE=serial_simple LOG_LEVEL=LOG_LEVEL_INFO (default)
 */

int main(void) {
    /* Initialize logging with UART backend
     * This single call:
     * - Initializes UART2 hardware (115200 baud, 8N1)
     * - Configures log_c to use UART for output
     * - Sets up the singleton logging context
     */
    log_platform_init_uart();
    
    /* Initialize LED for visual feedback */
    led2_init();
    
    /* Log initialization messages */
    loginfo("Hello, UART Terminal!");
    loginfo("UART initialized successfully!");
    loginfo("Starting LED blink test...");

    /* Main loop - blink LED and log periodically */
    uint32_t count = 0;
    while (1) {
        led2_off();
        systick_delay_ms(200);
        led2_on();
        systick_delay_ms(800);
        
        /* Log every tick for first 10, then every 10th tick */
        if (++count < 10) {
            loginfo("Tick... count=%lu", count);
        } else if (count % 10 == 0) {
            loginfo("Tick... count=%lu", count);
        }
    }
}
