#include "unity.h"
#include "fixed.h"

void setUp(void) {}
void tearDown(void) {}

/* --- q15_sat ------------------------------------------------------------- */

static void test_sat_in_range_unchanged(void)
{
    TEST_ASSERT_EQUAL_INT16(0, q15_sat(0));
    TEST_ASSERT_EQUAL_INT16(1234, q15_sat(1234));
    TEST_ASSERT_EQUAL_INT16(-1234, q15_sat(-1234));
    TEST_ASSERT_EQUAL_INT16(Q15_MAX, q15_sat((q31_t)Q15_MAX));
    TEST_ASSERT_EQUAL_INT16(Q15_MIN, q15_sat((q31_t)Q15_MIN));
}

static void test_sat_clamps_high_and_low(void)
{
    TEST_ASSERT_EQUAL_INT16(Q15_MAX, q15_sat(40000));
    TEST_ASSERT_EQUAL_INT16(Q15_MAX, q15_sat(0x7FFFFFFF));
    TEST_ASSERT_EQUAL_INT16(Q15_MIN, q15_sat(-40000));
    TEST_ASSERT_EQUAL_INT16(Q15_MIN, q15_sat((q31_t)0x80000000));
}

/* --- q15_add ------------------------------------------------------------- */

static void test_add_basic(void)
{
    TEST_ASSERT_EQUAL_INT16(300, q15_add(100, 200));
    TEST_ASSERT_EQUAL_INT16(0, q15_add(500, -500));
}

static void test_add_saturates(void)
{
    /* 0.75 + 0.75 would overflow +1.0 -> clamps to Q15_MAX. */
    TEST_ASSERT_EQUAL_INT16(Q15_MAX, q15_add(24576, 24576));
    /* -0.75 + -0.75 clamps to Q15_MIN. */
    TEST_ASSERT_EQUAL_INT16(Q15_MIN, q15_add(-24576, -24576));
}

/* --- q15_mul ------------------------------------------------------------- */

static void test_mul_identity_and_zero(void)
{
    /* x * (+1.0) ~= x (Q15_ONE is +0.99997, so allow 1 LSB of rounding). */
    TEST_ASSERT_INT16_WITHIN(1, 12345, q15_mul(12345, Q15_ONE));
    TEST_ASSERT_EQUAL_INT16(0, q15_mul(12345, 0));
}

static void test_mul_half_times_half_rounds(void)
{
    /* 0.5 * 0.5 = 0.25 -> 8192 in q15, with round-to-nearest. */
    TEST_ASSERT_EQUAL_INT16(8192, q15_mul(16384, 16384));
}

static void test_mul_minus_one_squared_saturates(void)
{
    /*
     * (-1.0) * (-1.0) = +1.0, but +1.0 is not representable in q15; the
     * q30 product 0x40000000 rounds/shifts to 0x8000 which MUST saturate to
     * Q15_MAX rather than wrap to Q15_MIN (the classic q15 overflow case).
     */
    TEST_ASSERT_EQUAL_INT16(Q15_MAX, q15_mul(Q15_MIN, Q15_MIN));
}

static void test_mul_sign(void)
{
    TEST_ASSERT_EQUAL_INT16(-8192, q15_mul(16384, -16384));
    TEST_ASSERT_EQUAL_INT16(-8192, q15_mul(-16384, 16384));
}

/* --- float conversions --------------------------------------------------- */

static void test_from_float_round_half_away_from_zero(void)
{
    TEST_ASSERT_EQUAL_INT16(0, q15_from_float(0.0f));
    /* 0.5 * 32768 = 16384 exactly. */
    TEST_ASSERT_EQUAL_INT16(16384, q15_from_float(0.5f));
    TEST_ASSERT_EQUAL_INT16(-16384, q15_from_float(-0.5f));
}

static void test_from_float_saturates(void)
{
    TEST_ASSERT_EQUAL_INT16(Q15_MAX, q15_from_float(2.0f));
    TEST_ASSERT_EQUAL_INT16(Q15_MIN, q15_from_float(-2.0f));
    /* Exactly +1.0 is out of range (32768) and must clamp to Q15_MAX. */
    TEST_ASSERT_EQUAL_INT16(Q15_MAX, q15_from_float(1.0f));
    /* Exactly -1.0 is representable. */
    TEST_ASSERT_EQUAL_INT16(Q15_MIN, q15_from_float(-1.0f));
}

static void test_to_float_roundtrip(void)
{
    for (q15_t v = -30000; v < 30000; v += 777) {
        float f = q15_to_float(v);
        TEST_ASSERT_INT16_WITHIN(1, v, q15_from_float(f));
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sat_in_range_unchanged);
    RUN_TEST(test_sat_clamps_high_and_low);
    RUN_TEST(test_add_basic);
    RUN_TEST(test_add_saturates);
    RUN_TEST(test_mul_identity_and_zero);
    RUN_TEST(test_mul_half_times_half_rounds);
    RUN_TEST(test_mul_minus_one_squared_saturates);
    RUN_TEST(test_mul_sign);
    RUN_TEST(test_from_float_round_half_away_from_zero);
    RUN_TEST(test_from_float_saturates);
    RUN_TEST(test_to_float_roundtrip);
    return UNITY_END();
}
