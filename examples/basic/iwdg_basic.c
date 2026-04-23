/*
 * iwdg_basic.c -- Independent Watchdog basic example.
 *
 * Demonstrates the IWDG driver by:
 *   1. Checking whether the previous reset was caused by the watchdog.
 *   2. Initialising the IWDG with a 1-second timeout.
 *   3. Feeding the watchdog in the main loop with a 500 ms period.
 *
 * The on-board LED (LED2, PA5) blinks at 500 ms to show the MCU is alive.
 * If the watchdog is not fed (e.g. by commenting out iwdg_feed()), the MCU
 * will reset after ~1 second and the cycle restarts.
 *
 * To simulate a hang, increase the delay to > 1000 ms. The watchdog will
 * trigger a reset and the LED pattern will change on the next boot (fast
 * blink indicating watchdog reset detected).
 */

#include "iwdg.h"
#include "led2.h"
#include "systick.h"

/* Feed interval in milliseconds. Keep below the 1000 ms watchdog timeout.
 * Set this above 1000 to intentionally trigger a watchdog reset. */
#define FEED_INTERVAL_MS    500U

/* Fast-blink count to signal that a watchdog reset was detected */
#define RESET_BLINK_COUNT   10U
#define RESET_BLINK_MS      100U

int main(void)
{
    led2_init();
    systick_init();

    /* Check if the last reset was caused by the watchdog */
    if (iwdg_was_reset_cause()) {
        iwdg_clear_reset_flags();

        /* Signal watchdog reset with fast LED blinks */
        for (uint32_t i = 0; i < RESET_BLINK_COUNT; i++) {
            led2_toggle();
            systick_delay_ms(RESET_BLINK_MS);
        }
    }

    /* Start the watchdog with a 1-second timeout */
    iwdg_init(1000);

    while (1) {
        led2_toggle();
        systick_delay_ms(FEED_INTERVAL_MS);

        /* Feed the watchdog to prevent reset.
         * Comment this line out to observe a watchdog reset. */
        iwdg_feed();
    }
}
