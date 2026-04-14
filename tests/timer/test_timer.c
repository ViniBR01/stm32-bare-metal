/*
 * test_timer.c — Host unit tests for drivers/src/timer.c
 *
 * Two tiers of tests:
 *
 * Tier 2 — Pure function tests (timer_calc.h)
 *   timer_compute_pwm_psc, timer_compute_duty_ccr
 *   No register access; test the mathematical logic directly.
 *
 * Tier 1 — Register configuration tests
 *   timer_init, timer_start, timer_stop, timer_set_period,
 *   timer_register_callback, timer_pwm_init, timer_pwm_set_duty.
 *   Uses fake peripheral structs to verify that each function writes the
 *   correct bits to the correct registers.
 *
 *   Note: timer_delay_us() is not tested here. It contains an unconditional
 *   busy-wait on TIM5->SR.UIF that cannot be driven by static fake registers.
 *
 * setUp() zeros all fakes and seeds a 16 MHz clock via rcc_init() so that
 * rcc_get_apb1_timer_clk() returns a known value for PWM tests.
 */

#include "unity.h"
#include "stm32f4xx.h"  /* stub: TypeDefs + fake peripheral declarations */
#include "rcc.h"
#include "timer.h"
#include "timer_calc.h"

/* Timer clock seeded in setUp() — 16 MHz (HSI direct, no PLL) */
#define TEST_TIMER_CLK_HZ  16000000U

/* Bit definitions mirrored from timer.c */
#define CR1_CEN   (1U << 0)
#define CR1_OPM   (1U << 3)
#define DIER_UIE  (1U << 0)
#define SR_UIF    (1U << 0)

/* ---- Test lifecycle ---------------------------------------------------- */

static int cb_fired;
static void test_cb(void) { cb_fired++; }

void setUp(void)
{
    test_periph_reset();
    cb_fired = 0;
    /* Seed clock cache so rcc_get_apb1_timer_clk() returns TEST_TIMER_CLK_HZ */
    rcc_init(RCC_CLK_SRC_HSI, TEST_TIMER_CLK_HZ);
}

void tearDown(void) {}

/* ======================================================================== */
/* timer_compute_pwm_psc                                                      */
/* ======================================================================== */

void test_pwm_psc_50mhz_1khz_100steps_is_499(void)
{
    /* 50 MHz / (1 kHz * 100) - 1 = 499 */
    TEST_ASSERT_EQUAL(499, timer_compute_pwm_psc(50000000U, 1000U, 100U));
}

void test_pwm_psc_100mhz_1khz_1000steps_is_99(void)
{
    TEST_ASSERT_EQUAL(99, timer_compute_pwm_psc(100000000U, 1000U, 1000U));
}

void test_pwm_psc_16mhz_1khz_100steps_is_159(void)
{
    /* 16 MHz / (1 kHz * 100) - 1 = 159 */
    TEST_ASSERT_EQUAL(159, timer_compute_pwm_psc(16000000U, 1000U, 100U));
}

void test_pwm_psc_16mhz_1khz_16steps_is_999(void)
{
    /* 16 MHz / (1 kHz * 16) - 1 = 999 */
    TEST_ASSERT_EQUAL(999, timer_compute_pwm_psc(16000000U, 1000U, 16U));
}

/* ======================================================================== */
/* timer_compute_duty_ccr                                                     */
/* ======================================================================== */

void test_duty_ccr_50pct_of_99_is_49(void)
{
    TEST_ASSERT_EQUAL(49, timer_compute_duty_ccr(99U, 50U));
}

void test_duty_ccr_0pct_is_0(void)
{
    TEST_ASSERT_EQUAL(0, timer_compute_duty_ccr(99U, 0U));
}

void test_duty_ccr_100pct_equals_arr(void)
{
    TEST_ASSERT_EQUAL(99, timer_compute_duty_ccr(99U, 100U));
}

void test_duty_ccr_50pct_of_255_is_127(void)
{
    TEST_ASSERT_EQUAL(127, timer_compute_duty_ccr(255U, 50U));
}

void test_duty_ccr_50pct_of_1000_is_500(void)
{
    TEST_ASSERT_EQUAL(500, timer_compute_duty_ccr(1000U, 50U));
}

void test_duty_ccr_above_100_clamped_to_arr(void)
{
    TEST_ASSERT_EQUAL(99, timer_compute_duty_ccr(99U, 101U));
}

void test_duty_ccr_200pct_clamped_to_arr(void)
{
    TEST_ASSERT_EQUAL(99, timer_compute_duty_ccr(99U, 200U));
}

/* ======================================================================== */
/* timer_init                                                                 */
/* ======================================================================== */

void test_timer_init_tim2_enables_apb1_clock(void)
{
    timer_init(TIMER_2, 499U, 999U);
    TEST_ASSERT_BITS_HIGH(RCC_APB1ENR_TIM2EN, fake_RCC.APB1ENR);
}

void test_timer_init_tim2_sets_psc(void)
{
    timer_init(TIMER_2, 499U, 999U);
    TEST_ASSERT_EQUAL(499U, fake_TIM2.PSC);
}

void test_timer_init_tim2_sets_arr(void)
{
    timer_init(TIMER_2, 499U, 999U);
    TEST_ASSERT_EQUAL(999U, fake_TIM2.ARR);
}

void test_timer_init_tim2_clears_cnt(void)
{
    fake_TIM2.CNT = 0xDEADU;
    timer_init(TIMER_2, 499U, 999U);
    TEST_ASSERT_EQUAL(0U, fake_TIM2.CNT);
}

void test_timer_init_tim3_enables_apb1_clock(void)
{
    timer_init(TIMER_3, 9U, 99U);
    TEST_ASSERT_BITS_HIGH(RCC_APB1ENR_TIM3EN, fake_RCC.APB1ENR);
}

void test_timer_init_tim3_sets_psc_and_arr(void)
{
    timer_init(TIMER_3, 9U, 99U);
    TEST_ASSERT_EQUAL(9U,  fake_TIM3.PSC);
    TEST_ASSERT_EQUAL(99U, fake_TIM3.ARR);
}

void test_timer_init_tim4_enables_apb1_clock(void)
{
    timer_init(TIMER_4, 0U, 255U);
    TEST_ASSERT_BITS_HIGH(RCC_APB1ENR_TIM4EN, fake_RCC.APB1ENR);
}

void test_timer_init_tim5_enables_apb1_clock(void)
{
    timer_init(TIMER_5, 0U, 0xFFFFFFFFU);
    TEST_ASSERT_BITS_HIGH(RCC_APB1ENR_TIM5EN, fake_RCC.APB1ENR);
}

void test_timer_init_does_not_clobber_other_timer(void)
{
    timer_init(TIMER_2, 1U, 2U);
    timer_init(TIMER_3, 3U, 4U);
    TEST_ASSERT_EQUAL(1U, fake_TIM2.PSC);
    TEST_ASSERT_EQUAL(2U, fake_TIM2.ARR);
    TEST_ASSERT_EQUAL(3U, fake_TIM3.PSC);
    TEST_ASSERT_EQUAL(4U, fake_TIM3.ARR);
}

/* ======================================================================== */
/* timer_start / timer_stop                                                   */
/* ======================================================================== */

void test_timer_start_sets_cen_bit(void)
{
    timer_init(TIMER_2, 0U, 999U);
    timer_start(TIMER_2);
    TEST_ASSERT_BITS_HIGH(CR1_CEN, fake_TIM2.CR1);
}

void test_timer_start_does_not_affect_other_timer(void)
{
    timer_start(TIMER_2);
    TEST_ASSERT_BITS_LOW(CR1_CEN, fake_TIM3.CR1);
}

void test_timer_stop_clears_cen_bit(void)
{
    fake_TIM2.CR1 = CR1_CEN;
    timer_stop(TIMER_2);
    TEST_ASSERT_BITS_LOW(CR1_CEN, fake_TIM2.CR1);
}

void test_timer_stop_does_not_affect_running_timer(void)
{
    fake_TIM2.CR1 = CR1_CEN;
    fake_TIM3.CR1 = CR1_CEN;
    timer_stop(TIMER_2);
    TEST_ASSERT_BITS_HIGH(CR1_CEN, fake_TIM3.CR1);
}

/* ======================================================================== */
/* timer_set_period                                                           */
/* ======================================================================== */

void test_timer_set_period_updates_arr(void)
{
    timer_init(TIMER_2, 0U, 999U);
    timer_set_period(TIMER_2, 1999U);
    TEST_ASSERT_EQUAL(1999U, fake_TIM2.ARR);
}

void test_timer_set_period_does_not_affect_psc(void)
{
    timer_init(TIMER_2, 42U, 999U);
    timer_set_period(TIMER_2, 1999U);
    TEST_ASSERT_EQUAL(42U, fake_TIM2.PSC);
}

/* ======================================================================== */
/* timer_register_callback                                                    */
/* ======================================================================== */

void test_register_callback_enables_update_interrupt(void)
{
    timer_init(TIMER_2, 0U, 999U);
    timer_register_callback(TIMER_2, test_cb);
    TEST_ASSERT_BITS_HIGH(DIER_UIE, fake_TIM2.DIER);
}

void test_register_callback_enables_nvic_irq(void)
{
    timer_init(TIMER_2, 0U, 999U);
    timer_register_callback(TIMER_2, test_cb);
    uint32_t irqn = (uint32_t)TIM2_IRQn;
    TEST_ASSERT_BITS_HIGH(1U << (irqn & 0x1FU), fake_NVIC.ISER[irqn >> 5U]);
}

void test_register_null_callback_disables_update_interrupt(void)
{
    timer_init(TIMER_2, 0U, 999U);
    timer_register_callback(TIMER_2, test_cb);
    timer_register_callback(TIMER_2, NULL);
    TEST_ASSERT_BITS_LOW(DIER_UIE, fake_TIM2.DIER);
}

void test_register_null_callback_disables_nvic_irq(void)
{
    timer_init(TIMER_2, 0U, 999U);
    timer_register_callback(TIMER_2, test_cb);
    timer_register_callback(TIMER_2, NULL);
    uint32_t irqn = (uint32_t)TIM2_IRQn;
    TEST_ASSERT_BITS_HIGH(1U << (irqn & 0x1FU), fake_NVIC.ICER[irqn >> 5U]);
}

void test_register_callback_tim5_enables_nvic_irq(void)
{
    timer_init(TIMER_5, 0U, 999U);
    timer_register_callback(TIMER_5, test_cb);
    uint32_t irqn = (uint32_t)TIM5_IRQn;
    TEST_ASSERT_BITS_HIGH(1U << (irqn & 0x1FU), fake_NVIC.ISER[irqn >> 5U]);
}

void test_register_callback_does_not_affect_other_timer_dier(void)
{
    timer_init(TIMER_2, 0U, 999U);
    timer_register_callback(TIMER_2, test_cb);
    TEST_ASSERT_BITS_LOW(DIER_UIE, fake_TIM3.DIER);
}

/* ======================================================================== */
/* timer_pwm_init                                                             */
/* ======================================================================== */

/*
 * With TEST_TIMER_CLK_HZ = 16 MHz, pwm_freq=1000 Hz, steps=100:
 *   PSC = timer_compute_pwm_psc(16M, 1000, 100) = 159
 *   ARR = steps - 1 = 99
 */
#define TEST_PWM_FREQ   1000U
#define TEST_PWM_STEPS  100U
#define TEST_PWM_PSC    159U   /* (16M / (1kHz * 100)) - 1 */
#define TEST_PWM_ARR    99U    /* steps - 1 */

void test_pwm_init_ch1_enables_apb1_clock(void)
{
    timer_pwm_init(TIMER_2, TIMER_CH1, TEST_PWM_FREQ, TEST_PWM_STEPS);
    TEST_ASSERT_BITS_HIGH(RCC_APB1ENR_TIM2EN, fake_RCC.APB1ENR);
}

void test_pwm_init_ch1_sets_psc(void)
{
    timer_pwm_init(TIMER_2, TIMER_CH1, TEST_PWM_FREQ, TEST_PWM_STEPS);
    TEST_ASSERT_EQUAL(TEST_PWM_PSC, fake_TIM2.PSC);
}

void test_pwm_init_ch1_sets_arr(void)
{
    timer_pwm_init(TIMER_2, TIMER_CH1, TEST_PWM_FREQ, TEST_PWM_STEPS);
    TEST_ASSERT_EQUAL(TEST_PWM_ARR, fake_TIM2.ARR);
}

void test_pwm_init_ch1_sets_pwm_mode_and_preload_in_ccmr1(void)
{
    /*
     * CH1, shift=0:
     *   OC1M = 110 → bits [6:4] of CCMR1 → mask 0x70, value 0x60
     *   OC1PE = 1  → bit  [3]   of CCMR1 → mask 0x08, value 0x08
     */
    timer_pwm_init(TIMER_2, TIMER_CH1, TEST_PWM_FREQ, TEST_PWM_STEPS);
    TEST_ASSERT_BITS_HIGH(0x68U, fake_TIM2.CCMR1);
}

void test_pwm_init_ch1_enables_output_in_ccer(void)
{
    timer_pwm_init(TIMER_2, TIMER_CH1, TEST_PWM_FREQ, TEST_PWM_STEPS);
    TEST_ASSERT_BITS_HIGH(1U << 0, fake_TIM2.CCER);
}

void test_pwm_init_ch1_sets_ccr1_to_zero(void)
{
    fake_TIM2.CCR1 = 0xFFFFU;
    timer_pwm_init(TIMER_2, TIMER_CH1, TEST_PWM_FREQ, TEST_PWM_STEPS);
    TEST_ASSERT_EQUAL(0U, fake_TIM2.CCR1);
}

void test_pwm_init_ch2_sets_pwm_mode_and_preload_in_ccmr1_upper(void)
{
    /*
     * CH2, shift=8:
     *   OC2M = 110 → bits [14:12] of CCMR1 → 0x6000
     *   OC2PE = 1  → bit  [11]    of CCMR1 → 0x0800
     */
    timer_pwm_init(TIMER_2, TIMER_CH2, TEST_PWM_FREQ, TEST_PWM_STEPS);
    TEST_ASSERT_BITS_HIGH(0x6800U, fake_TIM2.CCMR1);
}

void test_pwm_init_ch2_enables_output_in_ccer(void)
{
    timer_pwm_init(TIMER_2, TIMER_CH2, TEST_PWM_FREQ, TEST_PWM_STEPS);
    TEST_ASSERT_BITS_HIGH(1U << 4, fake_TIM2.CCER);
}

void test_pwm_init_ch3_sets_pwm_mode_in_ccmr2(void)
{
    /* CH3, shift=0 in CCMR2: same layout as CH1 in CCMR1 */
    timer_pwm_init(TIMER_2, TIMER_CH3, TEST_PWM_FREQ, TEST_PWM_STEPS);
    TEST_ASSERT_BITS_HIGH(0x68U, fake_TIM2.CCMR2);
}

void test_pwm_init_ch3_enables_output_in_ccer(void)
{
    timer_pwm_init(TIMER_2, TIMER_CH3, TEST_PWM_FREQ, TEST_PWM_STEPS);
    TEST_ASSERT_BITS_HIGH(1U << 8, fake_TIM2.CCER);
}

void test_pwm_init_ch4_sets_pwm_mode_in_ccmr2_upper(void)
{
    /* CH4, shift=8 in CCMR2 */
    timer_pwm_init(TIMER_2, TIMER_CH4, TEST_PWM_FREQ, TEST_PWM_STEPS);
    TEST_ASSERT_BITS_HIGH(0x6800U, fake_TIM2.CCMR2);
}

void test_pwm_init_ch4_enables_output_in_ccer(void)
{
    timer_pwm_init(TIMER_2, TIMER_CH4, TEST_PWM_FREQ, TEST_PWM_STEPS);
    TEST_ASSERT_BITS_HIGH(1U << 12, fake_TIM2.CCER);
}

void test_pwm_init_does_not_clobber_other_timer(void)
{
    timer_pwm_init(TIMER_2, TIMER_CH1, TEST_PWM_FREQ, TEST_PWM_STEPS);
    TEST_ASSERT_EQUAL(0U, fake_TIM3.PSC);
    TEST_ASSERT_EQUAL(0U, fake_TIM3.ARR);
}

/* ======================================================================== */
/* timer_pwm_set_duty                                                         */
/* ======================================================================== */

void test_pwm_set_duty_50pct_ch1(void)
{
    timer_pwm_init(TIMER_2, TIMER_CH1, TEST_PWM_FREQ, TEST_PWM_STEPS);
    /* ARR = 99 → CCR = (99 * 50) / 100 = 49 */
    timer_pwm_set_duty(TIMER_2, TIMER_CH1, 50U);
    TEST_ASSERT_EQUAL(49U, fake_TIM2.CCR1);
}

void test_pwm_set_duty_0pct_ch1_is_zero(void)
{
    timer_pwm_init(TIMER_2, TIMER_CH1, TEST_PWM_FREQ, TEST_PWM_STEPS);
    timer_pwm_set_duty(TIMER_2, TIMER_CH1, 0U);
    TEST_ASSERT_EQUAL(0U, fake_TIM2.CCR1);
}

void test_pwm_set_duty_100pct_ch1_equals_arr(void)
{
    timer_pwm_init(TIMER_2, TIMER_CH1, TEST_PWM_FREQ, TEST_PWM_STEPS);
    timer_pwm_set_duty(TIMER_2, TIMER_CH1, 100U);
    TEST_ASSERT_EQUAL(TEST_PWM_ARR, fake_TIM2.CCR1);
}

void test_pwm_set_duty_above_100_clamped_to_arr(void)
{
    timer_pwm_init(TIMER_2, TIMER_CH1, TEST_PWM_FREQ, TEST_PWM_STEPS);
    timer_pwm_set_duty(TIMER_2, TIMER_CH1, 150U);
    TEST_ASSERT_EQUAL(TEST_PWM_ARR, fake_TIM2.CCR1);
}

void test_pwm_set_duty_50pct_ch2(void)
{
    timer_pwm_init(TIMER_2, TIMER_CH2, TEST_PWM_FREQ, TEST_PWM_STEPS);
    timer_pwm_set_duty(TIMER_2, TIMER_CH2, 50U);
    TEST_ASSERT_EQUAL(49U, fake_TIM2.CCR2);
}

void test_pwm_set_duty_50pct_ch3(void)
{
    timer_pwm_init(TIMER_2, TIMER_CH3, TEST_PWM_FREQ, TEST_PWM_STEPS);
    timer_pwm_set_duty(TIMER_2, TIMER_CH3, 50U);
    TEST_ASSERT_EQUAL(49U, fake_TIM2.CCR3);
}

void test_pwm_set_duty_50pct_ch4(void)
{
    timer_pwm_init(TIMER_2, TIMER_CH4, TEST_PWM_FREQ, TEST_PWM_STEPS);
    timer_pwm_set_duty(TIMER_2, TIMER_CH4, 50U);
    TEST_ASSERT_EQUAL(49U, fake_TIM2.CCR4);
}

/* ======================================================================== */
/* main                                                                       */
/* ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* timer_compute_pwm_psc */
    RUN_TEST(test_pwm_psc_50mhz_1khz_100steps_is_499);
    RUN_TEST(test_pwm_psc_100mhz_1khz_1000steps_is_99);
    RUN_TEST(test_pwm_psc_16mhz_1khz_100steps_is_159);
    RUN_TEST(test_pwm_psc_16mhz_1khz_16steps_is_999);

    /* timer_compute_duty_ccr */
    RUN_TEST(test_duty_ccr_50pct_of_99_is_49);
    RUN_TEST(test_duty_ccr_0pct_is_0);
    RUN_TEST(test_duty_ccr_100pct_equals_arr);
    RUN_TEST(test_duty_ccr_50pct_of_255_is_127);
    RUN_TEST(test_duty_ccr_50pct_of_1000_is_500);
    RUN_TEST(test_duty_ccr_above_100_clamped_to_arr);
    RUN_TEST(test_duty_ccr_200pct_clamped_to_arr);

    /* timer_init */
    RUN_TEST(test_timer_init_tim2_enables_apb1_clock);
    RUN_TEST(test_timer_init_tim2_sets_psc);
    RUN_TEST(test_timer_init_tim2_sets_arr);
    RUN_TEST(test_timer_init_tim2_clears_cnt);
    RUN_TEST(test_timer_init_tim3_enables_apb1_clock);
    RUN_TEST(test_timer_init_tim3_sets_psc_and_arr);
    RUN_TEST(test_timer_init_tim4_enables_apb1_clock);
    RUN_TEST(test_timer_init_tim5_enables_apb1_clock);
    RUN_TEST(test_timer_init_does_not_clobber_other_timer);

    /* timer_start / timer_stop */
    RUN_TEST(test_timer_start_sets_cen_bit);
    RUN_TEST(test_timer_start_does_not_affect_other_timer);
    RUN_TEST(test_timer_stop_clears_cen_bit);
    RUN_TEST(test_timer_stop_does_not_affect_running_timer);

    /* timer_set_period */
    RUN_TEST(test_timer_set_period_updates_arr);
    RUN_TEST(test_timer_set_period_does_not_affect_psc);

    /* timer_register_callback */
    RUN_TEST(test_register_callback_enables_update_interrupt);
    RUN_TEST(test_register_callback_enables_nvic_irq);
    RUN_TEST(test_register_null_callback_disables_update_interrupt);
    RUN_TEST(test_register_null_callback_disables_nvic_irq);
    RUN_TEST(test_register_callback_tim5_enables_nvic_irq);
    RUN_TEST(test_register_callback_does_not_affect_other_timer_dier);

    /* timer_pwm_init */
    RUN_TEST(test_pwm_init_ch1_enables_apb1_clock);
    RUN_TEST(test_pwm_init_ch1_sets_psc);
    RUN_TEST(test_pwm_init_ch1_sets_arr);
    RUN_TEST(test_pwm_init_ch1_sets_pwm_mode_and_preload_in_ccmr1);
    RUN_TEST(test_pwm_init_ch1_enables_output_in_ccer);
    RUN_TEST(test_pwm_init_ch1_sets_ccr1_to_zero);
    RUN_TEST(test_pwm_init_ch2_sets_pwm_mode_and_preload_in_ccmr1_upper);
    RUN_TEST(test_pwm_init_ch2_enables_output_in_ccer);
    RUN_TEST(test_pwm_init_ch3_sets_pwm_mode_in_ccmr2);
    RUN_TEST(test_pwm_init_ch3_enables_output_in_ccer);
    RUN_TEST(test_pwm_init_ch4_sets_pwm_mode_in_ccmr2_upper);
    RUN_TEST(test_pwm_init_ch4_enables_output_in_ccer);
    RUN_TEST(test_pwm_init_does_not_clobber_other_timer);

    /* timer_pwm_set_duty */
    RUN_TEST(test_pwm_set_duty_50pct_ch1);
    RUN_TEST(test_pwm_set_duty_0pct_ch1_is_zero);
    RUN_TEST(test_pwm_set_duty_100pct_ch1_equals_arr);
    RUN_TEST(test_pwm_set_duty_above_100_clamped_to_arr);
    RUN_TEST(test_pwm_set_duty_50pct_ch2);
    RUN_TEST(test_pwm_set_duty_50pct_ch3);
    RUN_TEST(test_pwm_set_duty_50pct_ch4);

    return UNITY_END();
}
