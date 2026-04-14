/*
 * rcc_calc.h — Pure RCC calculation functions (no register access).
 *
 * Exposed for host unit testing. These functions implement the mathematical
 * logic behind rcc_init(): PLL factor selection, flash wait-state lookup,
 * and APB bus prescaler selection. They take plain integers and return plain
 * integers — no peripheral struct access, no side effects.
 */

#ifndef RCC_CALC_H
#define RCC_CALC_H

#include <stdint.h>

/**
 * PLL factors computed by rcc_compute_pll_config().
 *
 * All values are raw dividers / multipliers, not register encodings:
 *   PLLCFGR.PLLM  = pllm        (6-bit field, direct)
 *   PLLCFGR.PLLN  = plln        (9-bit field, direct)
 *   PLLCFGR.PLLP  = pllp/2 - 1 (2-bit field: 0→/2, 1→/4, 2→/6, 3→/8)
 *   PLLCFGR.PLLQ  = pllq        (4-bit field, direct)
 */
typedef struct {
    uint32_t pllm;  /**< Input divider  — VCO input = src / pllm, targeted at 2 MHz */
    uint32_t plln;  /**< VCO multiplier — must be in [50, 432] */
    uint32_t pllp;  /**< Output divider — raw value: 2, 4, 6, or 8 */
    uint32_t pllq;  /**< USB/SDIO clock divider — must be in [2, 15] */
} rcc_pll_factors_t;

/**
 * @brief Compute PLL factors to produce target_hz from src_hz.
 *
 * Targets a VCO input of 2 MHz (src / pllm). Tries PLLP values 2, 4, 6, 8
 * and picks the first that yields a valid integer PLLN in [50, 432] with
 * VCO output in [100, 432] MHz.
 *
 * @param src_hz    Source oscillator frequency in Hz (e.g. 16 000 000 for HSI)
 * @param target_hz Desired SYSCLK frequency in Hz
 * @param out       Filled on success; undefined on failure
 * @return 0 on success, -1 if no valid configuration exists
 */
int rcc_compute_pll_config(uint32_t src_hz, uint32_t target_hz,
                           rcc_pll_factors_t *out);

/**
 * @brief Return the number of flash wait states needed for hclk_hz.
 *
 * Table is valid at 2.7-3.6 V supply voltage (STM32F411 datasheet).
 * Returns 0-3 wait states corresponding to FLASH_ACR.LATENCY[3:0].
 *
 * @param hclk_hz HCLK (AHB bus) frequency in Hz
 * @return Number of wait states (0, 1, 2, or 3)
 */
uint32_t rcc_compute_flash_latency(uint32_t hclk_hz);

/**
 * @brief Return the smallest APB bus clock divider that satisfies
 *        hclk_hz / divider <= max_hz.
 *
 * Returns one of: 1, 2, 4, 8, 16. Used for both APB1 (max 50 MHz) and
 * APB2 (max 100 MHz) prescaler selection.
 *
 * @param hclk_hz HCLK (AHB) frequency in Hz
 * @param max_hz  Maximum allowed APB bus frequency in Hz
 * @return Bus clock divider (1, 2, 4, 8, or 16)
 */
uint32_t rcc_compute_apb_divider(uint32_t hclk_hz, uint32_t max_hz);

#endif /* RCC_CALC_H */
