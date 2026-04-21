#include "rcc.h"
#include "rcc_calc.h"
#include "stm32f4xx.h"

#define HSI_FREQ_HZ        16000000U
#define HSE_FREQ_HZ         8000000U

#define VCO_INPUT_TARGET     2000000U   /* 2 MHz for lowest PLL jitter */
#define VCO_OUTPUT_MIN     100000000U
#define VCO_OUTPUT_MAX     432000000U
#define SYSCLK_MAX         100000000U
#define APB1_MAX            50000000U
#define APB2_MAX           100000000U

#define PLL_LOCK_TIMEOUT   0x00FFFFFFU
#define HSE_READY_TIMEOUT  0x00FFFFFFU

/* Cached clock frequencies (set once during rcc_init) */
static uint32_t s_sysclk;
static uint32_t s_ahb_clk;
static uint32_t s_apb1_clk;
static uint32_t s_apb2_clk;
static uint32_t s_apb1_timer_clk;

/*
 * Flash latency table for STM32F411 at 2.7-3.6 V supply.
 * Index = wait states, value = max HCLK for that setting.
 */
static const uint32_t flash_max_freq[] = {
    30000000U,   /* 0 WS */
    64000000U,   /* 1 WS */
    90000000U,   /* 2 WS */
    100000000U,  /* 3 WS */
};

#define FLASH_WS_TABLE_SIZE  (sizeof(flash_max_freq) / sizeof(flash_max_freq[0]))

uint32_t rcc_compute_flash_latency(uint32_t hclk_hz) {
    for (uint32_t ws = 0; ws < FLASH_WS_TABLE_SIZE; ws++) {
        if (hclk_hz <= flash_max_freq[ws])
            return ws;
    }
    return FLASH_WS_TABLE_SIZE - 1;
}

/*
 * Determine the smallest APB prescaler (encoded for RCC_CFGR PPREx field)
 * such that hclk / divider <= max_freq.
 *
 * PPREx encoding: 0xx = /1, 100 = /2, 101 = /4, 110 = /8, 111 = /16
 * Returns { ppre_bits, divider } via out-parameters.
 */
static void compute_apb_prescaler(uint32_t hclk, uint32_t max_freq,
                                  uint32_t *ppre_bits, uint32_t *divider) {
    static const struct { uint32_t bits; uint32_t div; } table[] = {
        { 0x0U, 1 }, { 0x4U, 2 }, { 0x5U, 4 }, { 0x6U, 8 }, { 0x7U, 16 },
    };
    for (unsigned i = 0; i < sizeof(table)/sizeof(table[0]); i++) {
        if (hclk / table[i].div <= max_freq) {
            *ppre_bits = table[i].bits;
            *divider   = table[i].div;
            return;
        }
    }
    *ppre_bits = 0x7U;
    *divider   = 16;
}

uint32_t rcc_compute_apb_divider(uint32_t hclk_hz, uint32_t max_hz)
{
    uint32_t bits, div;
    compute_apb_prescaler(hclk_hz, max_hz, &bits, &div);
    (void)bits;
    return div;
}

err_t rcc_compute_pll_config(uint32_t src_hz, uint32_t target_hz,
                             rcc_pll_factors_t *out)
{
    uint32_t pllm   = src_hz / VCO_INPUT_TARGET;
    uint32_t vco_in = src_hz / pllm;

    uint32_t plln = 0, pllp = 0;
    for (uint32_t p = 2; p <= 8; p += 2) {
        uint32_t vco_out = target_hz * p;
        if (vco_out < VCO_OUTPUT_MIN || vco_out > VCO_OUTPUT_MAX)
            continue;
        uint32_t n = vco_out / vco_in;
        if (n * vco_in != vco_out)
            continue;
        if (n < 50 || n > 432)
            continue;
        plln = n;
        pllp = p;
        break;
    }
    if (plln == 0)
        return ERR_INVALID_ARG;

    uint32_t vco_out = vco_in * plln;
    uint32_t pllq    = vco_out / 48000000U;
    if (pllq < 2)  pllq = 2;
    if (pllq > 15) pllq = 15;

    out->pllm = pllm;
    out->plln = plln;
    out->pllp = pllp;
    out->pllq = pllq;
    return ERR_OK;
}

static void cache_default_clocks(uint32_t source_freq) {
    s_sysclk        = source_freq;
    s_ahb_clk       = source_freq;
    s_apb1_clk      = source_freq;
    s_apb2_clk      = source_freq;
    s_apb1_timer_clk = source_freq;
}

err_t rcc_init(rcc_clk_src_t source, uint32_t target_sysclk_hz) {
    uint32_t source_freq = (source == RCC_CLK_SRC_HSE_BYPASS)
                           ? HSE_FREQ_HZ : HSI_FREQ_HZ;

    /* No PLL needed — run directly from the oscillator */
    if (target_sysclk_hz == source_freq) {
        cache_default_clocks(source_freq);
        return ERR_OK;
    }

    if (target_sysclk_hz > SYSCLK_MAX)
        return ERR_INVALID_ARG;

    /* --- Compute PLL factors --- */
    rcc_pll_factors_t pll;
    if (rcc_compute_pll_config(source_freq, target_sysclk_hz, &pll) != ERR_OK)
        return ERR_INVALID_ARG;

    uint32_t pllm = pll.pllm;
    uint32_t plln = pll.plln;
    uint32_t pllp = pll.pllp;
    uint32_t pllq = pll.pllq;

    /* --- Compute bus prescalers --- */
    uint32_t ppre1_bits, ppre1_div;
    uint32_t ppre2_bits, ppre2_div;
    compute_apb_prescaler(target_sysclk_hz, APB1_MAX, &ppre1_bits, &ppre1_div);
    compute_apb_prescaler(target_sysclk_hz, APB2_MAX, &ppre2_bits, &ppre2_div);

    /* --- Set flash latency BEFORE increasing clock --- */
    uint32_t latency = rcc_compute_flash_latency(target_sysclk_hz);
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY)
               | latency
               | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;
    while ((FLASH->ACR & FLASH_ACR_LATENCY) != latency)
        ;

    /* --- Enable clock source --- */
    if (source == RCC_CLK_SRC_HSE_BYPASS) {
        RCC->CR |= RCC_CR_HSEBYP;
        RCC->CR |= RCC_CR_HSEON;
        for (uint32_t t = HSE_READY_TIMEOUT; t; t--) {
            if (RCC->CR & RCC_CR_HSERDY)
                break;
            if (t == 1) return ERR_TIMEOUT;
        }
    }
    /* HSI is on by default after reset — nothing to do for HSI */

    /* --- Disable PLL before reconfiguring --- */
    RCC->CR &= ~RCC_CR_PLLON;
    while (RCC->CR & RCC_CR_PLLRDY)
        ;

    /* --- Configure PLL --- */
    uint32_t pllcfgr = 0;
    pllcfgr |= (pllm          << RCC_PLLCFGR_PLLM_Pos) & RCC_PLLCFGR_PLLM_Msk;
    pllcfgr |= (plln          << RCC_PLLCFGR_PLLN_Pos) & RCC_PLLCFGR_PLLN_Msk;
    pllcfgr |= ((pllp/2 - 1)  << RCC_PLLCFGR_PLLP_Pos) & RCC_PLLCFGR_PLLP_Msk;
    pllcfgr |= (pllq          << RCC_PLLCFGR_PLLQ_Pos) & RCC_PLLCFGR_PLLQ_Msk;
    if (source == RCC_CLK_SRC_HSE_BYPASS)
        pllcfgr |= RCC_PLLCFGR_PLLSRC_HSE;
    else
        pllcfgr |= RCC_PLLCFGR_PLLSRC_HSI;

    RCC->PLLCFGR = pllcfgr;

    /* --- Enable PLL and wait for lock --- */
    RCC->CR |= RCC_CR_PLLON;
    for (uint32_t t = PLL_LOCK_TIMEOUT; t; t--) {
        if (RCC->CR & RCC_CR_PLLRDY)
            break;
        if (t == 1) return ERR_TIMEOUT;
    }

    /* --- Set bus prescalers --- */
    uint32_t cfgr = RCC->CFGR;
    cfgr &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2);
    cfgr |= RCC_CFGR_HPRE_DIV1;
    cfgr |= (ppre1_bits << RCC_CFGR_PPRE1_Pos);
    cfgr |= (ppre2_bits << RCC_CFGR_PPRE2_Pos);
    RCC->CFGR = cfgr;

    /* --- Switch system clock to PLL --- */
    cfgr = RCC->CFGR;
    cfgr &= ~RCC_CFGR_SW;
    cfgr |= RCC_CFGR_SW_PLL;
    RCC->CFGR = cfgr;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL)
        ;

    /* --- Cache resulting frequencies --- */
    s_sysclk        = target_sysclk_hz;
    s_ahb_clk       = target_sysclk_hz;   /* HPRE = /1 */
    s_apb1_clk      = target_sysclk_hz / ppre1_div;
    s_apb2_clk      = target_sysclk_hz / ppre2_div;
    s_apb1_timer_clk = (ppre1_div == 1) ? s_apb1_clk : s_apb1_clk * 2;

    return ERR_OK;
}

/*
 * CMSIS-standard entry point called from Reset_Handler before main().
 * Configures system clock to 100 MHz from HSI via PLL.
 *
 * Declared weak so that test_periph.c (host unit tests) can provide an
 * empty override without a linker duplicate-symbol error.
 */
__attribute__((weak)) void SystemInit(void) {
    /* Pre-cache HSI defaults so drivers have valid clock values
     * even if rcc_init fails (e.g. unsupported target frequency). */
    cache_default_clocks(HSI_FREQ_HZ);
    rcc_init(RCC_CLK_SRC_HSI, 100000000U);
}

uint32_t rcc_get_sysclk(void)        { return s_sysclk; }
uint32_t rcc_get_ahb_clk(void)       { return s_ahb_clk; }
uint32_t rcc_get_apb1_clk(void)      { return s_apb1_clk; }
uint32_t rcc_get_apb2_clk(void)      { return s_apb2_clk; }
uint32_t rcc_get_apb1_timer_clk(void) { return s_apb1_timer_clk; }
