/*
 * test_systick.c — Host unit tests for drivers/src/systick.c
 *
 * Tests cover:
 *
 * Tier 1 — Register configuration (systick_init)
 *   Verifies that systick_init() writes the correct LOAD, VAL, CTRL, and
 *   priority values to the fake SysTick and SCB structs.
 *
 * Tier 2 — Counter and elapsed time API
 *   Drives s_tick_ms via SysTick_Handler() calls and asserts that
 *   systick_get_ms() and systick_elapsed_since() return correct values,
 *   including 32-bit wraparound.
 *
 * Note: systick_delay_ms() is not tested here. It busy-waits until
 *   systick_get_ms() advances, which cannot be driven by a static fake.
 */

#include "unity.h"
#include "stm32f4xx.h"    /* stub: fake SysTick, SCB, NVIC via test_periph.h */
#include "irq_priorities.h"
#include "rcc.h"
#include "systick.h"

/* Expose the SysTick ISR so tests can simulate ticks */
void SysTick_Handler(void);

/* ---- Test clock (HSI direct, 16 MHz) ------------------------------------ */

#define TEST_SYSCLK_HZ   16000000U

/* Expected LOAD value: sysclk/1000 - 1 */
#define EXPECTED_LOAD   (TEST_SYSCLK_HZ / 1000U - 1U)

/* Expected CTRL value: CLKSOURCE | TICKINT | ENABLE */
#define EXPECTED_CTRL   (SysTick_CTRL_CLKSOURCE_Msk \
                        | SysTick_CTRL_TICKINT_Msk  \
                        | SysTick_CTRL_ENABLE_Msk)

/* Expected SHP index for SysTick_IRQn == -1:
 *   ((uint32_t)(-1) & 0xF) - 4 == (15 - 4) == 11  */
#define EXPECTED_SHP_IDX   11U
#define EXPECTED_SHP_VAL   ((uint8_t)((IRQ_PRIO_TIMER << (8U - 4U)) & 0xFFU))

/* ---- Lifecycle ---------------------------------------------------------- */

void setUp(void)
{
    test_periph_reset();
    systick_reset_for_test();
    /* Seed clock so rcc_get_sysclk() returns TEST_SYSCLK_HZ */
    rcc_init(RCC_CLK_SRC_HSI, TEST_SYSCLK_HZ);
}

void tearDown(void) {}

/* ======================================================================== */
/* systick_init — register configuration                                     */
/* ======================================================================== */

void test_init_sets_load_register(void)
{
    systick_init();
    TEST_ASSERT_EQUAL_UINT32(EXPECTED_LOAD, fake_SysTick.LOAD);
}

void test_init_clears_val_register(void)
{
    fake_SysTick.VAL = 0xDEADU;
    systick_init();
    TEST_ASSERT_EQUAL_UINT32(0U, fake_SysTick.VAL);
}

void test_init_sets_ctrl_clksource_bit(void)
{
    systick_init();
    TEST_ASSERT_BITS_HIGH(SysTick_CTRL_CLKSOURCE_Msk, fake_SysTick.CTRL);
}

void test_init_sets_ctrl_tickint_bit(void)
{
    systick_init();
    TEST_ASSERT_BITS_HIGH(SysTick_CTRL_TICKINT_Msk, fake_SysTick.CTRL);
}

void test_init_sets_ctrl_enable_bit(void)
{
    systick_init();
    TEST_ASSERT_BITS_HIGH(SysTick_CTRL_ENABLE_Msk, fake_SysTick.CTRL);
}

void test_init_sets_priority_via_scb_shp(void)
{
    systick_init();
    TEST_ASSERT_EQUAL_UINT8(EXPECTED_SHP_VAL, fake_SCB.SHP[EXPECTED_SHP_IDX]);
}

/* ======================================================================== */
/* systick_get_ms — tick counter                                             */
/* ======================================================================== */

void test_get_ms_returns_zero_before_any_tick(void)
{
    /* After setUp+reset, no ticks have fired */
    TEST_ASSERT_EQUAL_UINT32(0U, systick_get_ms());
}

void test_get_ms_returns_one_after_one_handler_call(void)
{
    SysTick_Handler();
    TEST_ASSERT_EQUAL_UINT32(1U, systick_get_ms());
}

void test_get_ms_returns_correct_count_after_many_ticks(void)
{
    for (int i = 0; i < 500; i++) SysTick_Handler();
    TEST_ASSERT_EQUAL_UINT32(500U, systick_get_ms());
}

void test_get_ms_accumulates_across_multiple_init_calls(void)
{
    /* systick_init() must not reset the counter */
    for (int i = 0; i < 10; i++) SysTick_Handler();
    systick_init();
    TEST_ASSERT_EQUAL_UINT32(10U, systick_get_ms());
}

/* ======================================================================== */
/* systick_elapsed_since — wraparound arithmetic                              */
/* ======================================================================== */

void test_elapsed_no_wraparound(void)
{
    /* Advance counter by 100 ticks */
    for (int i = 0; i < 100; i++) SysTick_Handler();
    uint32_t start = systick_get_ms();   /* = 100 */
    for (int i = 0; i < 250; i++) SysTick_Handler();
    TEST_ASSERT_EQUAL_UINT32(250U, systick_elapsed_since(start));
}

void test_elapsed_zero_when_start_equals_now(void)
{
    for (int i = 0; i < 42; i++) SysTick_Handler();
    uint32_t start = systick_get_ms();
    TEST_ASSERT_EQUAL_UINT32(0U, systick_elapsed_since(start));
}

void test_elapsed_handles_32bit_wraparound(void)
{
    /*
     * Simulate the counter sitting just below UINT32_MAX.
     * We cannot directly set s_tick_ms from outside the driver, so we
     * use a crafted start value and a known current value to test the
     * pure arithmetic:  current - start  (unsigned, wraps correctly).
     *
     * Set start = UINT32_MAX - 5, advance 10 ticks beyond wrap.
     * Expected elapsed = 10.
     *
     * We use the identity: systick_elapsed_since(start) == get_ms() - start.
     * Drive counter to UINT32_MAX - 5 + 10 = UINT32_MAX + 5 (wraps to 4).
     *
     * Since we cannot pre-seed s_tick_ms directly, we test the arithmetic
     * via a shim: call systick_elapsed_since with a synthetic start_ms.
     */

    /*
     * After setUp, counter == 0.
     * Advance counter by 10 ticks so get_ms() == 10.
     *
     * For wraparound: choose start_ms = UINT32_MAX - 2 (== 0xFFFFFFFD).
     * elapsed = current - start = 10 - 0xFFFFFFFD
     *         = 10 + 3 = 13  (unsigned 32-bit wraparound).
     */
    for (int i = 0; i < 10; i++) SysTick_Handler();

    uint32_t start_wrap = 0xFFFFFFFDU;   /* UINT32_MAX - 2 */
    /* get_ms() == 10; 10 - 0xFFFFFFFD = 13 (unsigned) */
    TEST_ASSERT_EQUAL_UINT32(13U, systick_elapsed_since(start_wrap));
}

void test_elapsed_one_tick_before_wrap(void)
{
    /* start = UINT32_MAX, current = 0 => elapsed = 1 (wraps from 0 - MAX) */
    uint32_t start_at_max = 0xFFFFFFFFU;
    /* Counter is still 0 from setUp (no ticks fired yet) */
    /* 0 - 0xFFFFFFFF = 1 (unsigned 32-bit) */
    TEST_ASSERT_EQUAL_UINT32(1U, systick_elapsed_since(start_at_max));
}

/* ======================================================================== */
/* Main                                                                       */
/* ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_init_sets_load_register);
    RUN_TEST(test_init_clears_val_register);
    RUN_TEST(test_init_sets_ctrl_clksource_bit);
    RUN_TEST(test_init_sets_ctrl_tickint_bit);
    RUN_TEST(test_init_sets_ctrl_enable_bit);
    RUN_TEST(test_init_sets_priority_via_scb_shp);

    RUN_TEST(test_get_ms_returns_zero_before_any_tick);
    RUN_TEST(test_get_ms_returns_one_after_one_handler_call);
    RUN_TEST(test_get_ms_returns_correct_count_after_many_ticks);
    RUN_TEST(test_get_ms_accumulates_across_multiple_init_calls);

    RUN_TEST(test_elapsed_no_wraparound);
    RUN_TEST(test_elapsed_zero_when_start_equals_now);
    RUN_TEST(test_elapsed_handles_32bit_wraparound);
    RUN_TEST(test_elapsed_one_tick_before_wrap);

    return UNITY_END();
}
