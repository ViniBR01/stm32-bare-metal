#include "unity.h"
#include "sincos.h"
#include "nco.h"
#include "vectors/sincos_golden.h"
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

/* --- LUT vs golden float reference --------------------------------------- */

static void test_sincos_matches_golden(void)
{
    for (size_t i = 0; i < SINCOS_GOLDEN_N; i++) {
        q15_t s, c;
        dsp_sincos_q15(sincos_golden[i].phase, &s, &c);
        TEST_ASSERT_INT16_WITHIN(2, sincos_golden[i].sin, s);
        TEST_ASSERT_INT16_WITHIN(2, sincos_golden[i].cos, c);
    }
}

/* --- known cardinal points ----------------------------------------------- */

static void test_sincos_cardinal_points(void)
{
    q15_t s, c;
    dsp_sincos_q15(0x00000000u, &s, &c);          /* 0     */
    TEST_ASSERT_INT16_WITHIN(2, 0, s);
    TEST_ASSERT_INT16_WITHIN(2, Q15_MAX, c);
    dsp_sincos_q15(0x40000000u, &s, &c);          /* pi/2  */
    TEST_ASSERT_INT16_WITHIN(2, Q15_MAX, s);
    TEST_ASSERT_INT16_WITHIN(2, 0, c);
    dsp_sincos_q15(0x80000000u, &s, &c);          /* pi    */
    TEST_ASSERT_INT16_WITHIN(2, 0, s);
    TEST_ASSERT_INT16_WITHIN(2, Q15_MIN, c);
    dsp_sincos_q15(0xC0000000u, &s, &c);          /* 3pi/2 */
    TEST_ASSERT_INT16_WITHIN(2, Q15_MIN, s);
    TEST_ASSERT_INT16_WITHIN(2, 0, c);
}

/* --- null-output safety -------------------------------------------------- */

static void test_sincos_null_outputs_safe(void)
{
    q15_t s = 123;
    dsp_sincos_q15(0x20000000u, &s, NULL);   /* cos NULL: must not crash */
    TEST_ASSERT_INT16_WITHIN(2, 23170, s);
    dsp_sincos_q15(0x20000000u, NULL, &s);   /* sin NULL */
    TEST_ASSERT_INT16_WITHIN(2, 23170, s);
}

/* --- phase-from-config helpers ------------------------------------------- */

static void test_phase_from_cycles(void)
{
    /* 0.25 cycle/sample == quarter circle == 2^30. */
    TEST_ASSERT_EQUAL_HEX32(0x40000000u, nco_phase_from_cycles(0.25f));
    /* Integer cycles wrap to 0. */
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, nco_phase_from_cycles(1.0f));
    /* 1.25 wraps to 0.25. */
    TEST_ASSERT_EQUAL_HEX32(0x40000000u, nco_phase_from_cycles(1.25f));
}

static void test_phase_from_rad(void)
{
    /* pi radians == half circle == 2^31. */
    uint32_t p = nco_phase_from_rad((float)M_PI);
    /* Allow a small float-rounding slack around 0x80000000. */
    TEST_ASSERT_UINT32_WITHIN(0x20000u, 0x80000000u, p);
}

/* --- NCO traces the unit circle ------------------------------------------ */

static void test_nco_unit_magnitude(void)
{
    /* An arbitrary increment; every step should land on the unit circle. */
    nco_t o;
    nco_init(&o, 0u, 0x06000000u);   /* ~0.0234 cycle/sample */
    for (int i = 0; i < 500; i++) {
        cq15_t z = nco_step(&o);
        double mag = sqrt((double)z.re * z.re + (double)z.im * z.im) / 32768.0;
        TEST_ASSERT_TRUE(fabs(mag - 1.0) < 0.01);   /* |z| ~ 1 */
    }
}

static void test_nco_phase_wraps(void)
{
    /* A near-full-circle increment must wrap cleanly past 2^32 with no jump. */
    nco_t o;
    nco_init(&o, 0xF0000000u, 0x20000000u);   /* will overflow uint32 */
    cq15_t a = nco_step(&o);   /* phase 0xF0000000 */
    cq15_t b = nco_step(&o);   /* phase 0x10000000 after wrap */
    /* cos at 0xF0000000 (=-pi/8) is positive; at 0x10000000 (=pi/8) also positive. */
    TEST_ASSERT_TRUE(a.re > 0);
    TEST_ASSERT_TRUE(b.re > 0);
    /* sin flips sign across the wrap (negative then positive). */
    TEST_ASSERT_TRUE(a.im < 0);
    TEST_ASSERT_TRUE(b.im > 0);
}

static void test_nco_constant_phase_when_incr_zero(void)
{
    nco_t o;
    nco_init(&o, 0x40000000u, 0u);   /* pi/2, no advance */
    cq15_t z1 = nco_step(&o);
    cq15_t z2 = nco_step(&o);
    TEST_ASSERT_EQUAL_INT16(z1.re, z2.re);
    TEST_ASSERT_EQUAL_INT16(z1.im, z2.im);
    TEST_ASSERT_INT16_WITHIN(2, Q15_MAX, z1.im);   /* sin(pi/2) = 1 */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sincos_matches_golden);
    RUN_TEST(test_sincos_cardinal_points);
    RUN_TEST(test_sincos_null_outputs_safe);
    RUN_TEST(test_phase_from_cycles);
    RUN_TEST(test_phase_from_rad);
    RUN_TEST(test_nco_unit_magnitude);
    RUN_TEST(test_nco_phase_wraps);
    RUN_TEST(test_nco_constant_phase_when_incr_zero);
    return UNITY_END();
}
