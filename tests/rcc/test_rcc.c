/*
 * test_rcc.c — Host unit tests for drivers/src/rcc.c
 *
 * Two tiers of tests:
 *
 * Tier 2 — Pure function tests (rcc_calc.h)
 *   rcc_compute_flash_latency, rcc_compute_apb_divider, rcc_compute_pll_config
 *   No register access; test the mathematical logic directly.
 *
 * Tier 1 — Register configuration tests
 *   Tests the no-PLL code path of rcc_init() (target == source frequency).
 *   The PLL path cannot be tested via register inspection because the
 *   PLL-lock and SWS busy-wait loops (0x00FFFFFF iterations each) would
 *   time out against static fake registers — that logic is covered by
 *   the Tier 2 pure-function tests above.
 *
 * setUp() zeroes all fake structs via test_periph_reset() before each test.
 * Bit-flag constants come from the real stm32f411xe.h device header.
 */

#include "unity.h"
#include "stm32f4xx.h"   /* stub: TypeDefs + fake peripheral declarations */
#include "rcc.h"
#include "rcc_calc.h"

void setUp(void)    { test_periph_reset(); }
void tearDown(void) {}

/* ======================================================================== */
/* rcc_compute_flash_latency                                                  */
/* ======================================================================== */

void test_flash_latency_at_16mhz_is_0ws(void)
{
    TEST_ASSERT_EQUAL(0, rcc_compute_flash_latency(16000000U));
}

void test_flash_latency_at_30mhz_boundary_is_0ws(void)
{
    TEST_ASSERT_EQUAL(0, rcc_compute_flash_latency(30000000U));
}

void test_flash_latency_above_30mhz_is_1ws(void)
{
    TEST_ASSERT_EQUAL(1, rcc_compute_flash_latency(30000001U));
}

void test_flash_latency_at_64mhz_boundary_is_1ws(void)
{
    TEST_ASSERT_EQUAL(1, rcc_compute_flash_latency(64000000U));
}

void test_flash_latency_above_64mhz_is_2ws(void)
{
    TEST_ASSERT_EQUAL(2, rcc_compute_flash_latency(64000001U));
}

void test_flash_latency_at_90mhz_boundary_is_2ws(void)
{
    TEST_ASSERT_EQUAL(2, rcc_compute_flash_latency(90000000U));
}

void test_flash_latency_above_90mhz_is_3ws(void)
{
    TEST_ASSERT_EQUAL(3, rcc_compute_flash_latency(90000001U));
}

void test_flash_latency_at_100mhz_is_3ws(void)
{
    TEST_ASSERT_EQUAL(3, rcc_compute_flash_latency(100000000U));
}

/* ======================================================================== */
/* rcc_compute_apb_divider                                                    */
/* ======================================================================== */

void test_apb_divider_100mhz_hclk_50mhz_max_gives_div2(void)
{
    TEST_ASSERT_EQUAL(2, rcc_compute_apb_divider(100000000U, 50000000U));
}

void test_apb_divider_100mhz_hclk_100mhz_max_gives_div1(void)
{
    TEST_ASSERT_EQUAL(1, rcc_compute_apb_divider(100000000U, 100000000U));
}

void test_apb_divider_16mhz_hclk_50mhz_max_gives_div1(void)
{
    TEST_ASSERT_EQUAL(1, rcc_compute_apb_divider(16000000U, 50000000U));
}

void test_apb_divider_64mhz_hclk_50mhz_max_gives_div2(void)
{
    TEST_ASSERT_EQUAL(2, rcc_compute_apb_divider(64000000U, 50000000U));
}

void test_apb_divider_200mhz_hclk_50mhz_max_gives_div4(void)
{
    TEST_ASSERT_EQUAL(4, rcc_compute_apb_divider(200000000U, 50000000U));
}

void test_apb_divider_exact_boundary_passes_without_extra_divide(void)
{
    /* 50 MHz / 1 = 50 MHz <= 50 MHz max → div 1 */
    TEST_ASSERT_EQUAL(1, rcc_compute_apb_divider(50000000U, 50000000U));
}

/* ======================================================================== */
/* rcc_compute_pll_config                                                     */
/* ======================================================================== */

/*
 * HSI (16 MHz) → 100 MHz
 *   PLLM = 16M / 2M = 8
 *   VCO input = 2 MHz
 *   p=2: VCO out = 200 MHz, PLLN = 100  (valid: 50-432, VCO 100-432 MHz)
 *   PLLQ = 200 MHz / 48 MHz = 4
 */
void test_pll_config_hsi_to_100mhz_pllm(void)
{
    rcc_pll_factors_t f;
    TEST_ASSERT_EQUAL(0, rcc_compute_pll_config(16000000U, 100000000U, &f));
    TEST_ASSERT_EQUAL(8, f.pllm);
}

void test_pll_config_hsi_to_100mhz_plln(void)
{
    rcc_pll_factors_t f;
    rcc_compute_pll_config(16000000U, 100000000U, &f);
    TEST_ASSERT_EQUAL(100, f.plln);
}

void test_pll_config_hsi_to_100mhz_pllp_is_2(void)
{
    rcc_pll_factors_t f;
    rcc_compute_pll_config(16000000U, 100000000U, &f);
    TEST_ASSERT_EQUAL(2, f.pllp);
}

void test_pll_config_hsi_to_100mhz_pllq(void)
{
    rcc_pll_factors_t f;
    rcc_compute_pll_config(16000000U, 100000000U, &f);
    TEST_ASSERT_EQUAL(4, f.pllq);  /* VCO 200 MHz / 48 MHz = 4 */
}

/*
 * HSE bypass (8 MHz) → 100 MHz
 *   PLLM = 8M / 2M = 4
 *   VCO input = 2 MHz
 *   p=2: VCO out = 200 MHz, PLLN = 100
 *   PLLQ = 4
 */
void test_pll_config_hse_to_100mhz_pllm_is_4(void)
{
    rcc_pll_factors_t f;
    TEST_ASSERT_EQUAL(0, rcc_compute_pll_config(8000000U, 100000000U, &f));
    TEST_ASSERT_EQUAL(4, f.pllm);
}

void test_pll_config_hse_to_100mhz_plln_is_100(void)
{
    rcc_pll_factors_t f;
    rcc_compute_pll_config(8000000U, 100000000U, &f);
    TEST_ASSERT_EQUAL(100, f.plln);
}

void test_pll_config_hse_to_100mhz_pllp_is_2(void)
{
    rcc_pll_factors_t f;
    rcc_compute_pll_config(8000000U, 100000000U, &f);
    TEST_ASSERT_EQUAL(2, f.pllp);
}

/*
 * HSI (16 MHz) → 96 MHz
 *   VCO out = 192 MHz, PLLN = 96, PLLP = 2, PLLQ = 4
 */
void test_pll_config_hsi_to_96mhz_plln_is_96(void)
{
    rcc_pll_factors_t f;
    TEST_ASSERT_EQUAL(0, rcc_compute_pll_config(16000000U, 96000000U, &f));
    TEST_ASSERT_EQUAL(96, f.plln);
}

void test_pll_config_hsi_to_96mhz_pllp_is_2(void)
{
    rcc_pll_factors_t f;
    rcc_compute_pll_config(16000000U, 96000000U, &f);
    TEST_ASSERT_EQUAL(2, f.pllp);
}

/*
 * Invalid / unreachable targets.
 */
void test_pll_config_target_3mhz_returns_error(void)
{
    rcc_pll_factors_t f;
    /* All PLLP values give VCO out < 100 MHz minimum */
    TEST_ASSERT_EQUAL(-1, rcc_compute_pll_config(16000000U, 3000000U, &f));
}

void test_pll_config_target_zero_returns_error(void)
{
    rcc_pll_factors_t f;
    TEST_ASSERT_EQUAL(-1, rcc_compute_pll_config(16000000U, 0U, &f));
}

/*
 * PLLQ clamping: PLLQ must be >= 2 even when VCO / 48 MHz < 2.
 *   HSI → 50 MHz: VCO out = 100 MHz. 100M / 48M = 2 (just at boundary).
 */
void test_pll_config_hsi_to_50mhz_pllq_clamped_to_2(void)
{
    rcc_pll_factors_t f;
    TEST_ASSERT_EQUAL(0, rcc_compute_pll_config(16000000U, 50000000U, &f));
    TEST_ASSERT_GREATER_OR_EQUAL(2, f.pllq);
}

/* ======================================================================== */
/* rcc_init — Tier 1 register tests (no-PLL code path only)                  */
/* ======================================================================== */

/*
 * When target == source frequency the PLL is bypassed entirely.
 * No busy-wait loops are executed; the function just caches clock values
 * and returns 0. This makes the no-PLL path safe to test with static fakes.
 */
void test_rcc_init_hsi_direct_returns_0(void)
{
    TEST_ASSERT_EQUAL(0, rcc_init(RCC_CLK_SRC_HSI, 16000000U));
}

void test_rcc_init_hsi_direct_caches_sysclk(void)
{
    rcc_init(RCC_CLK_SRC_HSI, 16000000U);
    TEST_ASSERT_EQUAL(16000000U, rcc_get_sysclk());
}

void test_rcc_init_hsi_direct_caches_ahb_clk(void)
{
    rcc_init(RCC_CLK_SRC_HSI, 16000000U);
    TEST_ASSERT_EQUAL(16000000U, rcc_get_ahb_clk());
}

void test_rcc_init_hsi_direct_caches_apb1_clk(void)
{
    rcc_init(RCC_CLK_SRC_HSI, 16000000U);
    TEST_ASSERT_EQUAL(16000000U, rcc_get_apb1_clk());
}

void test_rcc_init_hsi_direct_caches_apb2_clk(void)
{
    rcc_init(RCC_CLK_SRC_HSI, 16000000U);
    TEST_ASSERT_EQUAL(16000000U, rcc_get_apb2_clk());
}

void test_rcc_init_hsi_direct_caches_apb1_timer_clk(void)
{
    /* APB1 div = 1 → timer clock equals APB1 clock */
    rcc_init(RCC_CLK_SRC_HSI, 16000000U);
    TEST_ASSERT_EQUAL(16000000U, rcc_get_apb1_timer_clk());
}

void test_rcc_init_hse_direct_returns_0(void)
{
    TEST_ASSERT_EQUAL(0, rcc_init(RCC_CLK_SRC_HSE_BYPASS, 8000000U));
}

void test_rcc_init_hse_direct_caches_sysclk(void)
{
    rcc_init(RCC_CLK_SRC_HSE_BYPASS, 8000000U);
    TEST_ASSERT_EQUAL(8000000U, rcc_get_sysclk());
}

void test_rcc_init_target_exceeds_max_returns_error(void)
{
    /* 101 MHz > SYSCLK_MAX (100 MHz) — rejected immediately, no busy-waits */
    TEST_ASSERT_EQUAL(-1, rcc_init(RCC_CLK_SRC_HSI, 101000000U));
}

void test_rcc_init_unreachable_pll_target_returns_error(void)
{
    /*
     * 3 MHz: rcc_compute_pll_config returns -1 immediately (VCO output
     * would be < 100 MHz min for every valid PLLP value). No busy-waits.
     */
    TEST_ASSERT_EQUAL(-1, rcc_init(RCC_CLK_SRC_HSI, 3000000U));
}

/* ======================================================================== */
/* main                                                                       */
/* ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* rcc_compute_flash_latency */
    RUN_TEST(test_flash_latency_at_16mhz_is_0ws);
    RUN_TEST(test_flash_latency_at_30mhz_boundary_is_0ws);
    RUN_TEST(test_flash_latency_above_30mhz_is_1ws);
    RUN_TEST(test_flash_latency_at_64mhz_boundary_is_1ws);
    RUN_TEST(test_flash_latency_above_64mhz_is_2ws);
    RUN_TEST(test_flash_latency_at_90mhz_boundary_is_2ws);
    RUN_TEST(test_flash_latency_above_90mhz_is_3ws);
    RUN_TEST(test_flash_latency_at_100mhz_is_3ws);

    /* rcc_compute_apb_divider */
    RUN_TEST(test_apb_divider_100mhz_hclk_50mhz_max_gives_div2);
    RUN_TEST(test_apb_divider_100mhz_hclk_100mhz_max_gives_div1);
    RUN_TEST(test_apb_divider_16mhz_hclk_50mhz_max_gives_div1);
    RUN_TEST(test_apb_divider_64mhz_hclk_50mhz_max_gives_div2);
    RUN_TEST(test_apb_divider_200mhz_hclk_50mhz_max_gives_div4);
    RUN_TEST(test_apb_divider_exact_boundary_passes_without_extra_divide);

    /* rcc_compute_pll_config */
    RUN_TEST(test_pll_config_hsi_to_100mhz_pllm);
    RUN_TEST(test_pll_config_hsi_to_100mhz_plln);
    RUN_TEST(test_pll_config_hsi_to_100mhz_pllp_is_2);
    RUN_TEST(test_pll_config_hsi_to_100mhz_pllq);
    RUN_TEST(test_pll_config_hse_to_100mhz_pllm_is_4);
    RUN_TEST(test_pll_config_hse_to_100mhz_plln_is_100);
    RUN_TEST(test_pll_config_hse_to_100mhz_pllp_is_2);
    RUN_TEST(test_pll_config_hsi_to_96mhz_plln_is_96);
    RUN_TEST(test_pll_config_hsi_to_96mhz_pllp_is_2);
    RUN_TEST(test_pll_config_target_3mhz_returns_error);
    RUN_TEST(test_pll_config_target_zero_returns_error);
    RUN_TEST(test_pll_config_hsi_to_50mhz_pllq_clamped_to_2);

    /* rcc_init — no-PLL path */
    RUN_TEST(test_rcc_init_hsi_direct_returns_0);
    RUN_TEST(test_rcc_init_hsi_direct_caches_sysclk);
    RUN_TEST(test_rcc_init_hsi_direct_caches_ahb_clk);
    RUN_TEST(test_rcc_init_hsi_direct_caches_apb1_clk);
    RUN_TEST(test_rcc_init_hsi_direct_caches_apb2_clk);
    RUN_TEST(test_rcc_init_hsi_direct_caches_apb1_timer_clk);
    RUN_TEST(test_rcc_init_hse_direct_returns_0);
    RUN_TEST(test_rcc_init_hse_direct_caches_sysclk);
    RUN_TEST(test_rcc_init_target_exceeds_max_returns_error);
    RUN_TEST(test_rcc_init_unreachable_pll_target_returns_error);

    return UNITY_END();
}
