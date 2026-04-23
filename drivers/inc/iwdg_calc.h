/*
 * iwdg_calc.h -- Pure IWDG calculation functions (no register access).
 *
 * Exposed for host unit testing. These functions implement the mathematical
 * logic behind iwdg_init(): prescaler and reload value selection for a
 * desired watchdog timeout. They take plain integers and return plain
 * integers -- no peripheral struct access, no side effects.
 *
 * The IWDG is clocked by the LSI oscillator (~32 kHz, typical 32000 Hz).
 * Prescaler options: /4, /8, /16, /32, /64, /128, /256 (PR register 0..6).
 * Reload register (RLR): 12-bit, range 0..4095.
 *
 * Timeout formula:
 *   timeout_ms = (reload + 1) * prescaler_div / lsi_hz * 1000
 *
 * So:
 *   reload = (timeout_ms * lsi_hz) / (prescaler_div * 1000) - 1
 */

#ifndef IWDG_CALC_H
#define IWDG_CALC_H

#include <stdint.h>
#include "error.h"

/** Maximum reload register value (12-bit) */
#define IWDG_RLR_MAX    4095U

/** Default LSI frequency in Hz (nominal for STM32F411) */
#define IWDG_LSI_HZ     32000U

/** Number of available prescaler settings */
#define IWDG_PR_COUNT    7U

/**
 * @brief Result of iwdg_compute_config().
 */
typedef struct {
    uint32_t pr;      /**< Prescaler register value (0..6 -> /4, /8, ... /256) */
    uint32_t reload;  /**< Reload register value (0..4095) */
} iwdg_config_t;

/**
 * @brief Compute IWDG prescaler and reload values for a desired timeout.
 *
 * Iterates prescaler options from smallest (/4) to largest (/256) and picks
 * the first that yields a valid reload value in [0, 4095].
 *
 * The actual timeout achieved may differ slightly from the requested value
 * due to integer rounding. Use iwdg_compute_timeout_ms() to query the
 * exact timeout for a given config.
 *
 * @param timeout_ms  Desired watchdog timeout in milliseconds (must be >= 1)
 * @param lsi_hz      LSI oscillator frequency in Hz (typically 32000)
 * @param out         Filled on success; undefined on failure
 * @return ERR_OK on success, ERR_INVALID_ARG if timeout is 0 or too large
 */
err_t iwdg_compute_config(uint32_t timeout_ms, uint32_t lsi_hz,
                          iwdg_config_t *out);

/**
 * @brief Compute the actual timeout in milliseconds for a given config.
 *
 * @param pr      Prescaler register value (0..6)
 * @param reload  Reload register value (0..4095)
 * @param lsi_hz  LSI oscillator frequency in Hz
 * @return Timeout in milliseconds
 */
uint32_t iwdg_compute_timeout_ms(uint32_t pr, uint32_t reload, uint32_t lsi_hz);

/**
 * @brief Return the prescaler divider for a given PR register value.
 *
 * PR=0 -> 4, PR=1 -> 8, ..., PR=6 -> 256.
 * Returns 0 for invalid PR values (>= 7).
 *
 * @param pr  Prescaler register value (0..6)
 * @return Divider value, or 0 if pr is invalid
 */
uint32_t iwdg_prescaler_divider(uint32_t pr);

#endif /* IWDG_CALC_H */
