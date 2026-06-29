#include "unity.h"
#include "agc.h"
#include "complexq15.h"
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

/* AGC drives a small constant input up toward the reference magnitude. */
static void test_agc_amplifies_weak_signal(void)
{
    agc_t a;
    agc_init(&a, 0.5f, 0.01f, 1.0f);   /* target 0.5, slow step */

    /* Weak real input at 0.1 (3277 counts). The loop time constant is
     * ~1/(mu*|x|) = 1000 steps, so allow several time constants to converge. */
    cq15_t x = cq15_from_real(3277);
    cq15_t y = cq15_make(0, 0);
    for (int i = 0; i < 8000; i++) {
        y = agc_apply(&a, x);
    }
    float mag = sqrtf((float)y.re * y.re + (float)y.im * y.im) / 32768.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.03f, 0.5f, mag);
}

/* AGC attenuates a strong input down toward the reference. */
static void test_agc_attenuates_strong_signal(void)
{
    agc_t a;
    agc_init(&a, 0.3f, 0.01f, 1.0f);

    cq15_t x = cq15_from_real(30000);   /* ~0.92 */
    cq15_t y = cq15_make(0, 0);
    for (int i = 0; i < 2000; i++) {
        y = agc_apply(&a, x);
    }
    float mag = sqrtf((float)y.re * y.re + (float)y.im * y.im) / 32768.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.03f, 0.3f, mag);
}

/* Gain converges and then holds steady (loop is stable, not oscillating). */
static void test_agc_converges_and_holds(void)
{
    agc_t a;
    agc_init(&a, 0.5f, 0.02f, 1.0f);
    cq15_t x = cq15_from_real(8192);   /* 0.25 */
    for (int i = 0; i < 3000; i++) {
        agc_apply(&a, x);
    }
    float g1 = a.gain;
    for (int i = 0; i < 500; i++) {
        agc_apply(&a, x);
    }
    /* Gain barely moves once converged. */
    TEST_ASSERT_FLOAT_WITHIN(0.02f, g1, a.gain);
    /* Converged gain ~ ref/|x| = 0.5/0.25 = 2.0. */
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 2.0f, a.gain);
}

static void test_agc_null_safe(void)
{
    cq15_t x = cq15_from_real(100);
    cq15_t y = agc_apply(NULL, x);
    TEST_ASSERT_EQUAL_INT16(100, y.re);
    TEST_PASS();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_agc_amplifies_weak_signal);
    RUN_TEST(test_agc_attenuates_strong_signal);
    RUN_TEST(test_agc_converges_and_holds);
    RUN_TEST(test_agc_null_safe);
    return UNITY_END();
}
