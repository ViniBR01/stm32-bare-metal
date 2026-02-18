#ifndef RCC_H
#define RCC_H

#include <stdint.h>

/**
 * @brief Clock source selection for rcc_init
 */
typedef enum {
    RCC_CLK_SRC_HSI,          /**< 16 MHz internal RC oscillator */
    RCC_CLK_SRC_HSE_BYPASS,   /**< External clock via ST-LINK MCO (8 MHz on Nucleo) */
} rcc_clk_src_t;

/**
 * @brief Configure system clocks via PLL to reach the target frequency.
 *
 * Automatically computes PLL factors (PLLM/PLLN/PLLP), flash wait states,
 * and AHB/APB1/APB2 prescalers for the requested SYSCLK frequency.
 *
 * Constraints (STM32F411):
 *   - VCO input  = source / PLLM  must be 1-2 MHz (driver targets 2 MHz)
 *   - VCO output = VCO_in * PLLN  must be 100-432 MHz
 *   - SYSCLK     = VCO_out / PLLP must be <= 100 MHz
 *   - APB1 max 50 MHz, APB2 max 100 MHz
 *
 * If target_sysclk_hz equals the source frequency (no PLL needed),
 * the PLL is bypassed and the source is used directly.
 *
 * @param source          Clock source (HSI or HSE bypass)
 * @param target_sysclk_hz  Desired SYSCLK frequency in Hz (e.g. 100000000)
 * @return 0 on success, -1 on invalid parameters or PLL lock timeout
 */
int rcc_init(rcc_clk_src_t source, uint32_t target_sysclk_hz);

/** @return Current SYSCLK frequency in Hz */
uint32_t rcc_get_sysclk(void);

/** @return Current AHB (HCLK) frequency in Hz */
uint32_t rcc_get_ahb_clk(void);

/** @return Current APB1 peripheral clock frequency in Hz */
uint32_t rcc_get_apb1_clk(void);

/** @return Current APB2 peripheral clock frequency in Hz */
uint32_t rcc_get_apb2_clk(void);

/**
 * @return Current APB1 timer clock in Hz.
 *         When the APB1 prescaler is != 1, timers on APB1 run at 2x the
 *         APB1 peripheral clock (per the STM32F4 clock tree).
 */
uint32_t rcc_get_apb1_timer_clk(void);

#endif /* RCC_H */
