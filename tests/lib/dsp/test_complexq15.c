#include "unity.h"
#include "complexq15.h"
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

/* Float reference for a complex multiply, rounded back to q15 per component. */
static void ref_mul(cq15_t a, cq15_t b, double *re, double *im)
{
    double ar = q15_to_float(a.re), ai = q15_to_float(a.im);
    double br = q15_to_float(b.re), bi = q15_to_float(b.im);
    *re = ar * br - ai * bi;
    *im = ar * bi + ai * br;
}

/* --- construction / accessors -------------------------------------------- */

static void test_make_and_accessors(void)
{
    cq15_t c = cq15_make(1000, -2000);
    TEST_ASSERT_EQUAL_INT16(1000, c.re);
    TEST_ASSERT_EQUAL_INT16(-2000, c.im);
    TEST_ASSERT_EQUAL_INT16(1000, cq15_real(c));

    cq15_t r = cq15_from_real(1234);
    TEST_ASSERT_EQUAL_INT16(1234, r.re);
    TEST_ASSERT_EQUAL_INT16(0, r.im);
}

/* --- addition ------------------------------------------------------------ */

static void test_add_and_saturation(void)
{
    cq15_t s = cq15_add(cq15_make(100, 200), cq15_make(50, -50));
    TEST_ASSERT_EQUAL_INT16(150, s.re);
    TEST_ASSERT_EQUAL_INT16(150, s.im);

    /* Each component saturates independently. */
    cq15_t hi = cq15_add(cq15_make(24576, -24576), cq15_make(24576, -24576));
    TEST_ASSERT_EQUAL_INT16(Q15_MAX, hi.re);
    TEST_ASSERT_EQUAL_INT16(Q15_MIN, hi.im);
}

/* --- multiply vs float reference ----------------------------------------- */

static void test_mul_matches_float_reference(void)
{
    /* Sweep a grid of complex values; q15 result must match the float
     * reference (also rounded to q15) within 2 LSB on each component. */
    for (int ar = -30000; ar <= 30000; ar += 7919) {
        for (int ai = -30000; ai <= 30000; ai += 6113) {
            for (int br = -30000; br <= 30000; br += 8101) {
                cq15_t a = cq15_make((q15_t)ar, (q15_t)ai);
                cq15_t b = cq15_make((q15_t)br, (q15_t)(ai / 2));
                cq15_t got = cq15_mul(a, b);
                double re, im;
                ref_mul(a, b, &re, &im);
                TEST_ASSERT_INT16_WITHIN(2, q15_from_float((float)re), got.re);
                TEST_ASSERT_INT16_WITHIN(2, q15_from_float((float)im), got.im);
            }
        }
    }
}

static void test_mul_by_one_is_identity(void)
{
    cq15_t one = cq15_make(Q15_MAX, 0);
    cq15_t a = cq15_make(12345, -6789);
    cq15_t got = cq15_mul(a, one);
    /* Q15_MAX is +0.99997, so expect a within ~1 LSB. */
    TEST_ASSERT_INT16_WITHIN(2, a.re, got.re);
    TEST_ASSERT_INT16_WITHIN(2, a.im, got.im);
}

static void test_mul_by_j_rotates_90deg(void)
{
    /* j * (re + j im) = -im + j re. Use j = (0, +1.0). */
    cq15_t j = cq15_make(0, Q15_MAX);
    cq15_t a = cq15_make(20000, 10000);
    cq15_t got = cq15_mul(a, j);
    TEST_ASSERT_INT16_WITHIN(2, -10000, got.re);
    TEST_ASSERT_INT16_WITHIN(2, 20000, got.im);
}

static void test_mul_worst_case_saturates(void)
{
    /* (-1-j)^2 = 2j: real part 0, imag +2.0 -> saturates to Q15_MAX. */
    cq15_t m = cq15_make(Q15_MIN, Q15_MIN);
    cq15_t got = cq15_mul(m, m);
    TEST_ASSERT_INT16_WITHIN(2, 0, got.re);
    TEST_ASSERT_EQUAL_INT16(Q15_MAX, got.im);
}

/* --- conjugate / scale / dot --------------------------------------------- */

static void test_conj(void)
{
    cq15_t c = cq15_conj(cq15_make(1000, -2000));
    TEST_ASSERT_EQUAL_INT16(1000, c.re);
    TEST_ASSERT_EQUAL_INT16(2000, c.im);
    /* conj of Q15_MIN imag saturates (no overflow). */
    cq15_t c2 = cq15_conj(cq15_make(0, Q15_MIN));
    TEST_ASSERT_EQUAL_INT16(Q15_MAX, c2.im);
}

static void test_scale_real(void)
{
    /* Scale by 0.5 halves both components. */
    cq15_t got = cq15_scale_real(cq15_make(20000, -8000), 16384);
    TEST_ASSERT_INT16_WITHIN(2, 10000, got.re);
    TEST_ASSERT_INT16_WITHIN(2, -4000, got.im);
}

static void test_dot_re(void)
{
    /* Re{a conj(b)} = a.re*b.re + a.im*b.im, returned as q30 accumulator. */
    cq15_t a = cq15_make(16384, 8192);   /* 0.5 + j0.25 */
    cq15_t b = cq15_make(16384, 8192);
    q31_t d = cq15_dot_re(a, b);
    /* 0.5*0.5 + 0.25*0.25 = 0.3125 in q30 = 0.3125 * 2^30. */
    double expect = 0.3125 * (double)(1 << 30);
    TEST_ASSERT_TRUE(fabs((double)d - expect) < 4.0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_make_and_accessors);
    RUN_TEST(test_add_and_saturation);
    RUN_TEST(test_mul_matches_float_reference);
    RUN_TEST(test_mul_by_one_is_identity);
    RUN_TEST(test_mul_by_j_rotates_90deg);
    RUN_TEST(test_mul_worst_case_saturates);
    RUN_TEST(test_conj);
    RUN_TEST(test_scale_real);
    RUN_TEST(test_dot_re);
    return UNITY_END();
}
