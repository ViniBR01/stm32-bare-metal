#include "unity.h"
#include "bpsk.h"
#include "prbs.h"

void setUp(void) {}
void tearDown(void) {}

/* --- map ----------------------------------------------------------------- */

static void test_map_bit0_is_negative_one(void)
{
    TEST_ASSERT_EQUAL_INT16(Q15_MIN, bpsk_map(0u));
}

static void test_map_bit1_is_positive_one(void)
{
    TEST_ASSERT_EQUAL_INT16(Q15_ONE, bpsk_map(1u));
}

static void test_map_ignores_high_bits(void)
{
    /* Only the LSB matters. */
    TEST_ASSERT_EQUAL_INT16(BPSK_SYM_LO, bpsk_map(0xFEu));
    TEST_ASSERT_EQUAL_INT16(BPSK_SYM_HI, bpsk_map(0xFFu));
}

/* --- slice --------------------------------------------------------------- */

static void test_slice_positive_is_1(void)
{
    TEST_ASSERT_EQUAL_UINT8(1u, bpsk_slice(Q15_ONE));
    TEST_ASSERT_EQUAL_UINT8(1u, bpsk_slice(1));
}

static void test_slice_negative_is_0(void)
{
    TEST_ASSERT_EQUAL_UINT8(0u, bpsk_slice(Q15_MIN));
    TEST_ASSERT_EQUAL_UINT8(0u, bpsk_slice(-1));
}

static void test_slice_zero_tie_resolves_to_1(void)
{
    /* Documented convention: exactly 0 -> bit 1 (non-negative half-plane). */
    TEST_ASSERT_EQUAL_UINT8(1u, bpsk_slice(0));
}

/* --- round-trip ---------------------------------------------------------- */

static void test_map_slice_roundtrip_both_bits(void)
{
    TEST_ASSERT_EQUAL_UINT8(0u, bpsk_slice(bpsk_map(0u)));
    TEST_ASSERT_EQUAL_UINT8(1u, bpsk_slice(bpsk_map(1u)));
}

static void test_block_roundtrip_against_prbs(void)
{
    prbs_t p;
    prbs_init(&p, PRBS9, 0xABCu);

    uint8_t bits[256];
    uint8_t out[256];
    q15_t   syms[256];

    prbs_next_bits(&p, bits, 256);
    bpsk_map_block(bits, syms, 256);
    bpsk_slice_block(syms, out, 256);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(bits, out, 256);
}

static void test_block_null_args_safe(void)
{
    q15_t syms[4];
    uint8_t bits[4];
    bpsk_map_block(NULL, syms, 4);
    bpsk_slice_block(NULL, bits, 4);
    bpsk_map_block(bits, NULL, 4);
    bpsk_slice_block(syms, NULL, 4);
    TEST_PASS();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_map_bit0_is_negative_one);
    RUN_TEST(test_map_bit1_is_positive_one);
    RUN_TEST(test_map_ignores_high_bits);
    RUN_TEST(test_slice_positive_is_1);
    RUN_TEST(test_slice_negative_is_0);
    RUN_TEST(test_slice_zero_tie_resolves_to_1);
    RUN_TEST(test_map_slice_roundtrip_both_bits);
    RUN_TEST(test_block_roundtrip_against_prbs);
    RUN_TEST(test_block_null_args_safe);
    return UNITY_END();
}
