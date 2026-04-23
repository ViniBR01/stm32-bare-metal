/*
 * iwdg.c -- Independent Watchdog (IWDG) driver for STM32F411.
 *
 * Register summary (IWDG at 0x4000 3000):
 *   KR  (0x00) -- Key register (write-only)
 *                  0x5555 = enable write access to PR and RLR
 *                  0xAAAA = reload the counter (feed)
 *                  0xCCCC = start the watchdog
 *   PR  (0x04) -- Prescaler register (3-bit, 0..6)
 *   RLR (0x08) -- Reload register (12-bit, 0..4095)
 *   SR  (0x0C) -- Status register
 *                  bit 0 (PVU) = prescaler value update in progress
 *                  bit 1 (RVU) = reload value update in progress
 *
 * The IWDG counter counts down from RLR. When it reaches 0, the MCU resets.
 * Writing 0xAAAA to KR reloads the counter with the RLR value.
 *
 * Clock source: LSI oscillator (~32 kHz, not precise).
 */

#include "iwdg.h"
#include "iwdg_calc.h"
#include "stm32f4xx.h"

/* ======================================================================== */
/* Pure calculation functions (iwdg_calc.h) -- no register access           */
/* ======================================================================== */

/*
 * Prescaler divider lookup: PR register value 0..6 maps to /4../256.
 * Formula: divider = 4 << pr
 */
static const uint32_t s_prescaler_div[IWDG_PR_COUNT] = {
    4, 8, 16, 32, 64, 128, 256
};

uint32_t iwdg_prescaler_divider(uint32_t pr)
{
    if (pr >= IWDG_PR_COUNT) {
        return 0;
    }
    return s_prescaler_div[pr];
}

uint32_t iwdg_compute_timeout_ms(uint32_t pr, uint32_t reload, uint32_t lsi_hz)
{
    if (pr >= IWDG_PR_COUNT || lsi_hz == 0) {
        return 0;
    }
    uint32_t div = s_prescaler_div[pr];
    /* timeout_ms = (reload + 1) * div * 1000 / lsi_hz */
    return ((reload + 1U) * div * 1000U) / lsi_hz;
}

err_t iwdg_compute_config(uint32_t timeout_ms, uint32_t lsi_hz,
                          iwdg_config_t *out)
{
    if (timeout_ms == 0 || lsi_hz == 0 || out == (void *)0) {
        return ERR_INVALID_ARG;
    }

    /*
     * For each prescaler (smallest first), compute the reload value:
     *   reload = (timeout_ms * lsi_hz) / (div * 1000) - 1
     *
     * We want the smallest prescaler that yields reload <= IWDG_RLR_MAX
     * for best resolution.
     */
    for (uint32_t pr = 0; pr < IWDG_PR_COUNT; pr++) {
        uint32_t div = s_prescaler_div[pr];
        uint32_t denom = div * 1000U;

        /* ticks = timeout_ms * lsi_hz / (div * 1000)
         * Use 64-bit intermediate to avoid overflow for large timeouts */
        uint64_t ticks_64 = ((uint64_t)timeout_ms * lsi_hz + denom - 1U) / denom;

        if (ticks_64 == 0) {
            /* Timeout too small for this prescaler -- try next */
            continue;
        }

        /* reload = ticks - 1 */
        uint64_t reload_64 = ticks_64 - 1U;

        if (reload_64 <= IWDG_RLR_MAX) {
            out->pr     = pr;
            out->reload = (uint32_t)reload_64;
            return ERR_OK;
        }
    }

    /* No valid prescaler/reload combination found -- timeout too large */
    return ERR_INVALID_ARG;
}

/* ======================================================================== */
/* Hardware driver functions                                                 */
/* ======================================================================== */

/* IWDG key register magic values */
#define IWDG_KEY_ENABLE_ACCESS  0x5555U
#define IWDG_KEY_RELOAD         0xAAAAU
#define IWDG_KEY_START          0xCCCCU

/* Timeout for waiting on SR update flags (generous upper bound) */
#define IWDG_UPDATE_TIMEOUT     0x000FFFFFU

err_t iwdg_init(uint32_t timeout_ms)
{
    iwdg_config_t cfg;
    err_t err = iwdg_compute_config(timeout_ms, IWDG_LSI_HZ, &cfg);
    if (err != ERR_OK) {
        return err;
    }

    /* Enable write access to PR and RLR registers */
    IWDG->KR = IWDG_KEY_ENABLE_ACCESS;

    /* Set prescaler and reload value */
    IWDG->PR  = cfg.pr;
    IWDG->RLR = cfg.reload;

    /* Wait for the prescaler and reload values to be applied.
     * PVU and RVU bits are set while the update is in progress. */
    for (uint32_t t = IWDG_UPDATE_TIMEOUT; t; t--) {
        if ((IWDG->SR & (IWDG_SR_PVU | IWDG_SR_RVU)) == 0U) {
            break;
        }
        if (t == 1U) {
            return ERR_TIMEOUT;
        }
    }

    /* Reload the counter and start the watchdog */
    IWDG->KR = IWDG_KEY_RELOAD;
    IWDG->KR = IWDG_KEY_START;

    return ERR_OK;
}

void iwdg_feed(void)
{
    IWDG->KR = IWDG_KEY_RELOAD;
}

bool iwdg_was_reset_cause(void)
{
    return (RCC->CSR & RCC_CSR_IWDGRSTF) != 0U;
}

void iwdg_clear_reset_flags(void)
{
    RCC->CSR |= RCC_CSR_RMVF;
}
