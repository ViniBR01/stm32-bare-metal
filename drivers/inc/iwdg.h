/*
 * iwdg.h -- Independent Watchdog (IWDG) driver for STM32F411.
 *
 * The IWDG provides a hardware safety mechanism that resets the MCU if
 * software fails to refresh the watchdog counter within its timeout period.
 * It is clocked by the LSI oscillator (~32 kHz) and runs independently of
 * the main system clock.
 *
 * IMPORTANT: Once started, the IWDG cannot be stopped except by a system
 * reset. This is a hardware restriction -- there is no disable function.
 *
 * Usage:
 *   iwdg_init(1000);    // 1-second timeout
 *   while (1) {
 *       // ... application work ...
 *       iwdg_feed();    // must be called before timeout expires
 *   }
 */

#ifndef IWDG_H
#define IWDG_H

#include <stdint.h>
#include <stdbool.h>
#include "error.h"

/**
 * @brief Initialise and start the Independent Watchdog.
 *
 * Configures the prescaler and reload register for the requested timeout,
 * then starts the watchdog. Once started, iwdg_feed() must be called
 * periodically to prevent a system reset.
 *
 * Supported timeout range (at 32 kHz LSI):
 *   Minimum: ~0.125 ms  (PR=0 /4, RLR=0)
 *   Maximum: ~32768 ms  (PR=6 /256, RLR=4095)
 *
 * @param timeout_ms  Desired timeout in milliseconds (1..32768)
 * @return ERR_OK on success,
 *         ERR_INVALID_ARG if timeout_ms is 0 or exceeds the maximum,
 *         ERR_TIMEOUT if the prescaler/reload update did not complete
 */
err_t iwdg_init(uint32_t timeout_ms);

/**
 * @brief Refresh (feed/kick) the watchdog counter.
 *
 * Reloads the IWDG counter with the configured reload value, preventing
 * a watchdog reset. Must be called periodically -- at least once per
 * timeout period.
 */
void iwdg_feed(void);

/**
 * @brief Check whether the last system reset was caused by the IWDG.
 *
 * Reads the RCC_CSR IWDGRSTF flag. The flag is sticky and persists until
 * cleared by writing RMVF to RCC_CSR. This function does NOT clear the flag.
 *
 * @return true if the IWDG caused the last reset, false otherwise
 */
bool iwdg_was_reset_cause(void);

/**
 * @brief Clear all reset cause flags in RCC_CSR.
 *
 * Writes the RMVF bit to clear IWDGRSTF and all other reset flags.
 * Call this after reading the reset cause to avoid stale flag detection
 * on subsequent checks.
 */
void iwdg_clear_reset_flags(void);

#endif /* IWDG_H */
