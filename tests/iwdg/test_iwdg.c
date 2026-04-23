/*
 * test_iwdg.c -- Host unit tests for drivers/src/iwdg.c
 *
 * Two tiers of tests:
 *
 * Tier 2 -- Pure function tests (iwdg_calc.h)
 *   iwdg_prescaler_divider, iwdg_compute_timeout_ms, iwdg_compute_config
 *   No register access; test the mathematical logic directly.
 *
 * Tier 1 -- Register configuration tests
 *   Tests iwdg_init() register writes, iwdg_feed(), iwdg_was_reset_cause(),
 *   and iwdg_clear_reset_flags() against fake peripheral structs.
 *
 * setUp() zeroes all fake structs via test_periph_reset() before each test.
 */

#include "unity.h"
#include "stm32f4xx.h"   /* stub: TypeDefs + fake peripheral declarations */
#include "error.h"
#include "iwdg.h"
#include "iwdg_calc.h"

void setUp(void)    { test_periph_reset(); }
void tearDown(void) {}

/* ======================================================================== */
/* iwdg_prescaler_divider                                                    */
/* ======================================================================== */

void test_prescaler_divider_pr0_is_4(void)
{
    TEST_ASSERT_EQUAL(4, iwdg_prescaler_divider(0));
}

void test_prescaler_divider_pr1_is_8(void)
{
    TEST_ASSERT_EQUAL(8, iwdg_prescaler_divider(1));
}

void test_prescaler_divider_pr2_is_16(void)
{
    TEST_ASSERT_EQUAL(16, iwdg_prescaler_divider(2));
}

void test_prescaler_divider_pr3_is_32(void)
{
    TEST_ASSERT_EQUAL(32, iwdg_prescaler_divider(3));
}

void test_prescaler_divider_pr4_is_64(void)
{
    TEST_ASSERT_EQUAL(64, iwdg_prescaler_divider(4));
}

void test_prescaler_divider_pr5_is_128(void)
{
    TEST_ASSERT_EQUAL(128, iwdg_prescaler_divider(5));
}

void test_prescaler_divider_pr6_is_256(void)
{
    TEST_ASSERT_EQUAL(256, iwdg_prescaler_divider(6));
}

void test_prescaler_divider_pr7_invalid_returns_0(void)
{
    TEST_ASSERT_EQUAL(0, iwdg_prescaler_divider(7));
}

void test_prescaler_divider_pr255_invalid_returns_0(void)
{
    TEST_ASSERT_EQUAL(0, iwdg_prescaler_divider(255));
}

/* ======================================================================== */
/* iwdg_compute_timeout_ms                                                   */
/* ======================================================================== */

void test_timeout_pr0_rlr0_gives_minimum(void)
{
    /* (0+1) * 4 * 1000 / 32000 = 0 (integer truncation) */
    TEST_ASSERT_EQUAL(0, iwdg_compute_timeout_ms(0, 0, 32000));
}

void test_timeout_pr0_rlr7_gives_1ms(void)
{
    /* (7+1) * 4 * 1000 / 32000 = 1 */
    TEST_ASSERT_EQUAL(1, iwdg_compute_timeout_ms(0, 7, 32000));
}

void test_timeout_pr0_rlr4095_gives_512ms(void)
{
    /* (4095+1) * 4 * 1000 / 32000 = 512 */
    TEST_ASSERT_EQUAL(512, iwdg_compute_timeout_ms(0, 4095, 32000));
}

void test_timeout_pr6_rlr4095_gives_max(void)
{
    /* (4095+1) * 256 * 1000 / 32000 = 32768 */
    TEST_ASSERT_EQUAL(32768, iwdg_compute_timeout_ms(6, 4095, 32000));
}

void test_timeout_pr3_rlr999(void)
{
    /* (999+1) * 32 * 1000 / 32000 = 1000 */
    TEST_ASSERT_EQUAL(1000, iwdg_compute_timeout_ms(3, 999, 32000));
}

void test_timeout_invalid_pr_returns_0(void)
{
    TEST_ASSERT_EQUAL(0, iwdg_compute_timeout_ms(7, 100, 32000));
}

void test_timeout_zero_lsi_returns_0(void)
{
    TEST_ASSERT_EQUAL(0, iwdg_compute_timeout_ms(0, 100, 0));
}

/* ======================================================================== */
/* iwdg_compute_config                                                       */
/* ======================================================================== */

void test_config_1000ms_uses_pr1(void)
{
    /* 1000*32000/(8*1000) = 4000 -> reload=3999, fits in PR=1 (/8) */
    iwdg_config_t cfg;
    TEST_ASSERT_EQUAL(ERR_OK, iwdg_compute_config(1000, 32000, &cfg));
    TEST_ASSERT_EQUAL(1, cfg.pr);     /* /8 prescaler */
    TEST_ASSERT_EQUAL(3999, cfg.reload);
}

void test_config_500ms_uses_pr0(void)
{
    /* 500*32000/(4*1000) = 4000 -> reload=3999, fits in PR=0 (/4) */
    iwdg_config_t cfg;
    TEST_ASSERT_EQUAL(ERR_OK, iwdg_compute_config(500, 32000, &cfg));
    TEST_ASSERT_EQUAL(0, cfg.pr);
    TEST_ASSERT_EQUAL(3999, cfg.reload);
}

void test_config_100ms_uses_pr0(void)
{
    iwdg_config_t cfg;
    TEST_ASSERT_EQUAL(ERR_OK, iwdg_compute_config(100, 32000, &cfg));
    /* 100 * 32000 / (4 * 1000) = 800 -> reload = 799 */
    TEST_ASSERT_EQUAL(0, cfg.pr);
    TEST_ASSERT_EQUAL(799, cfg.reload);
}

void test_config_max_timeout_32768ms(void)
{
    iwdg_config_t cfg;
    TEST_ASSERT_EQUAL(ERR_OK, iwdg_compute_config(32768, 32000, &cfg));
    TEST_ASSERT_EQUAL(6, cfg.pr);     /* /256 prescaler */
    TEST_ASSERT_EQUAL(4095, cfg.reload);
}

void test_config_1ms_uses_pr0(void)
{
    iwdg_config_t cfg;
    TEST_ASSERT_EQUAL(ERR_OK, iwdg_compute_config(1, 32000, &cfg));
    /* 1 * 32000 / (4 * 1000) = 8.0 -> ceil -> 8 -> reload = 7 */
    TEST_ASSERT_EQUAL(0, cfg.pr);
    TEST_ASSERT_EQUAL(7, cfg.reload);
}

void test_config_2000ms(void)
{
    /* 2000*32000/(16*1000) = 4000 -> reload=3999, fits in PR=2 (/16) */
    iwdg_config_t cfg;
    TEST_ASSERT_EQUAL(ERR_OK, iwdg_compute_config(2000, 32000, &cfg));
    TEST_ASSERT_EQUAL(2, cfg.pr);
    TEST_ASSERT_EQUAL(3999, cfg.reload);
}

void test_config_10000ms(void)
{
    iwdg_config_t cfg;
    TEST_ASSERT_EQUAL(ERR_OK, iwdg_compute_config(10000, 32000, &cfg));
    /* PR=5 (/128): 10000*32000/(128*1000) = 2500 -> reload=2499 */
    TEST_ASSERT_EQUAL(5, cfg.pr);
    TEST_ASSERT_EQUAL(2499, cfg.reload);
}

void test_config_timeout_0_returns_invalid(void)
{
    iwdg_config_t cfg;
    TEST_ASSERT_EQUAL(ERR_INVALID_ARG, iwdg_compute_config(0, 32000, &cfg));
}

void test_config_timeout_too_large_returns_invalid(void)
{
    iwdg_config_t cfg;
    /* 40000 ms is beyond max (~32768 ms at 32 kHz) */
    TEST_ASSERT_EQUAL(ERR_INVALID_ARG, iwdg_compute_config(40000, 32000, &cfg));
}

void test_config_null_output_returns_invalid(void)
{
    TEST_ASSERT_EQUAL(ERR_INVALID_ARG, iwdg_compute_config(1000, 32000, (void *)0));
}

void test_config_zero_lsi_returns_invalid(void)
{
    iwdg_config_t cfg;
    TEST_ASSERT_EQUAL(ERR_INVALID_ARG, iwdg_compute_config(1000, 0, &cfg));
}

void test_config_prefers_smallest_prescaler(void)
{
    /* 512 ms fits in PR=0 (/4): 512*32000/(4*1000) = 4096 -> reload=4095 */
    iwdg_config_t cfg;
    TEST_ASSERT_EQUAL(ERR_OK, iwdg_compute_config(512, 32000, &cfg));
    TEST_ASSERT_EQUAL(0, cfg.pr);
    TEST_ASSERT_EQUAL(4095, cfg.reload);
}

void test_config_513ms_needs_pr1(void)
{
    /* 513 ms at /4: 513*32000/(4*1000) = 4104 > 4095 -- needs PR=1 */
    iwdg_config_t cfg;
    TEST_ASSERT_EQUAL(ERR_OK, iwdg_compute_config(513, 32000, &cfg));
    TEST_ASSERT_EQUAL(1, cfg.pr);
}

void test_config_nonstandard_lsi_40000(void)
{
    /* LSI can vary. Test with 40 kHz. 1000ms at /8:
     * 1000*40000/(8*1000) = 5000 > 4095 -- needs PR=2 (/16):
     * 1000*40000/(16*1000) = 2500 -> reload=2499 */
    iwdg_config_t cfg;
    TEST_ASSERT_EQUAL(ERR_OK, iwdg_compute_config(1000, 40000, &cfg));
    TEST_ASSERT_EQUAL(2, cfg.pr);
    TEST_ASSERT_EQUAL(2499, cfg.reload);
}

/* ======================================================================== */
/* iwdg_init -- register-level tests                                         */
/* ======================================================================== */

void test_init_1000ms_writes_correct_registers(void)
{
    /* SR=0 means no update in progress, so the wait loop exits immediately */
    err_t err = iwdg_init(1000);
    TEST_ASSERT_EQUAL(ERR_OK, err);

    /* PR=1 (/8), RLR=3999 for 1000ms at 32kHz */
    TEST_ASSERT_EQUAL(1, fake_IWDG.PR);
    TEST_ASSERT_EQUAL(3999, fake_IWDG.RLR);

    /* KR should have been written with START (0xCCCC) last */
    TEST_ASSERT_EQUAL_HEX32(0xCCCC, fake_IWDG.KR);
}

void test_init_invalid_timeout_returns_error(void)
{
    TEST_ASSERT_EQUAL(ERR_INVALID_ARG, iwdg_init(0));
}

void test_init_too_large_timeout_returns_error(void)
{
    TEST_ASSERT_EQUAL(ERR_INVALID_ARG, iwdg_init(40000));
}

/* ======================================================================== */
/* iwdg_feed                                                                 */
/* ======================================================================== */

void test_feed_writes_reload_key(void)
{
    iwdg_feed();
    TEST_ASSERT_EQUAL_HEX32(0xAAAA, fake_IWDG.KR);
}

/* ======================================================================== */
/* iwdg_was_reset_cause / iwdg_clear_reset_flags                             */
/* ======================================================================== */

void test_reset_cause_false_when_flag_clear(void)
{
    fake_RCC.CSR = 0;
    TEST_ASSERT_FALSE(iwdg_was_reset_cause());
}

void test_reset_cause_true_when_flag_set(void)
{
    fake_RCC.CSR = RCC_CSR_IWDGRSTF;
    TEST_ASSERT_TRUE(iwdg_was_reset_cause());
}

void test_clear_reset_flags_sets_rmvf(void)
{
    fake_RCC.CSR = RCC_CSR_IWDGRSTF;
    iwdg_clear_reset_flags();
    TEST_ASSERT_BITS_HIGH(RCC_CSR_RMVF, fake_RCC.CSR);
}

/* ======================================================================== */
/* main -- test runner                                                       */
/* ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* iwdg_prescaler_divider */
    RUN_TEST(test_prescaler_divider_pr0_is_4);
    RUN_TEST(test_prescaler_divider_pr1_is_8);
    RUN_TEST(test_prescaler_divider_pr2_is_16);
    RUN_TEST(test_prescaler_divider_pr3_is_32);
    RUN_TEST(test_prescaler_divider_pr4_is_64);
    RUN_TEST(test_prescaler_divider_pr5_is_128);
    RUN_TEST(test_prescaler_divider_pr6_is_256);
    RUN_TEST(test_prescaler_divider_pr7_invalid_returns_0);
    RUN_TEST(test_prescaler_divider_pr255_invalid_returns_0);

    /* iwdg_compute_timeout_ms */
    RUN_TEST(test_timeout_pr0_rlr0_gives_minimum);
    RUN_TEST(test_timeout_pr0_rlr7_gives_1ms);
    RUN_TEST(test_timeout_pr0_rlr4095_gives_512ms);
    RUN_TEST(test_timeout_pr6_rlr4095_gives_max);
    RUN_TEST(test_timeout_pr3_rlr999);
    RUN_TEST(test_timeout_invalid_pr_returns_0);
    RUN_TEST(test_timeout_zero_lsi_returns_0);

    /* iwdg_compute_config */
    RUN_TEST(test_config_1000ms_uses_pr1);
    RUN_TEST(test_config_500ms_uses_pr0);
    RUN_TEST(test_config_100ms_uses_pr0);
    RUN_TEST(test_config_max_timeout_32768ms);
    RUN_TEST(test_config_1ms_uses_pr0);
    RUN_TEST(test_config_2000ms);
    RUN_TEST(test_config_10000ms);
    RUN_TEST(test_config_timeout_0_returns_invalid);
    RUN_TEST(test_config_timeout_too_large_returns_invalid);
    RUN_TEST(test_config_null_output_returns_invalid);
    RUN_TEST(test_config_zero_lsi_returns_invalid);
    RUN_TEST(test_config_prefers_smallest_prescaler);
    RUN_TEST(test_config_513ms_needs_pr1);
    RUN_TEST(test_config_nonstandard_lsi_40000);

    /* Register-level tests */
    RUN_TEST(test_init_1000ms_writes_correct_registers);
    RUN_TEST(test_init_invalid_timeout_returns_error);
    RUN_TEST(test_init_too_large_timeout_returns_error);
    RUN_TEST(test_feed_writes_reload_key);
    RUN_TEST(test_reset_cause_false_when_flag_clear);
    RUN_TEST(test_reset_cause_true_when_flag_set);
    RUN_TEST(test_clear_reset_flags_sets_rmvf);

    return UNITY_END();
}
