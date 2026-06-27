#include "unity.h"
#include "prbs.h"

void setUp(void) {}
void tearDown(void) {}

/* --- init / period ------------------------------------------------------- */

static void test_prbs9_period_value(void)
{
    prbs_t p;
    uint32_t period = prbs_init(&p, PRBS9, 1u);
    TEST_ASSERT_EQUAL_UINT32(511u, period);
}

static void test_prbs15_period_value(void)
{
    prbs_t p;
    uint32_t period = prbs_init(&p, PRBS15, 1u);
    TEST_ASSERT_EQUAL_UINT32(32767u, period);
}

static void test_zero_seed_is_forced_nonzero(void)
{
    prbs_t p;
    prbs_init(&p, PRBS9, 0u);
    TEST_ASSERT_NOT_EQUAL(0u, p.state);
    /* A nonzero state must produce a sequence that is not stuck at 0. */
    uint8_t ones = 0;
    for (int i = 0; i < 64; i++) {
        ones |= prbs_next_bit(&p);
    }
    TEST_ASSERT_EQUAL_UINT8(1u, ones);
}

/* --- maximal-length: full period before repeat --------------------------- */

/*
 * A maximal-length LFSR visits every nonzero state exactly once per period.
 * Verify PRBS-9 returns to its seed state after exactly 511 steps and not
 * before.
 */
static void test_prbs9_returns_after_full_period(void)
{
    prbs_t p;
    prbs_init(&p, PRBS9, 0x1FFu);
    uint16_t seed_state = p.state;

    int returned_early = 0;
    for (uint32_t i = 0; i < 510u; i++) {
        prbs_next_bit(&p);
        if (p.state == seed_state) {
            returned_early = 1;
            break;
        }
    }
    TEST_ASSERT_FALSE(returned_early);

    prbs_next_bit(&p); /* 511th step */
    TEST_ASSERT_EQUAL_UINT16(seed_state, p.state);
}

/*
 * Over one full period a maximal-length sequence has 2^(n-1) ones and
 * 2^(n-1)-1 zeros. For PRBS-9: 256 ones, 255 zeros.
 */
static void test_prbs9_ones_balance_over_period(void)
{
    prbs_t p;
    prbs_init(&p, PRBS9, 1u);
    uint32_t ones = 0;
    for (uint32_t i = 0; i < 511u; i++) {
        ones += prbs_next_bit(&p);
    }
    TEST_ASSERT_EQUAL_UINT32(256u, ones);
}

/* --- next_bits matches next_bit ------------------------------------------ */

static void test_next_bits_matches_single(void)
{
    prbs_t a, b;
    prbs_init(&a, PRBS15, 0xACE1u);
    prbs_init(&b, PRBS15, 0xACE1u);

    uint8_t buf[100];
    prbs_next_bits(&a, buf, 100);
    for (int i = 0; i < 100; i++) {
        TEST_ASSERT_EQUAL_UINT8(prbs_next_bit(&b), buf[i]);
    }
}

static void test_next_bits_null_is_safe(void)
{
    prbs_t p;
    prbs_init(&p, PRBS9, 1u);
    prbs_next_bits(&p, NULL, 10); /* must not crash */
    TEST_PASS();
}

/* --- checker ------------------------------------------------------------- */

static void test_checker_clean_stream_zero_errors(void)
{
    prbs_t tx;
    prbs_check_t chk;
    prbs_init(&tx, PRBS9, 0x55u);
    prbs_check_init(&chk, PRBS9, 0x55u);

    for (int i = 0; i < 2000; i++) {
        uint8_t bit = prbs_next_bit(&tx);
        uint8_t ok = prbs_check_bit(&chk, bit);
        TEST_ASSERT_EQUAL_UINT8(1u, ok);
    }
    TEST_ASSERT_EQUAL_UINT64(2000u, chk.total);
    TEST_ASSERT_EQUAL_UINT64(0u, chk.errors);
}

static void test_checker_counts_injected_errors(void)
{
    prbs_t tx;
    prbs_check_t chk;
    prbs_init(&tx, PRBS15, 0x1234u);
    prbs_check_init(&chk, PRBS15, 0x1234u);

    /* Flip every 7th bit; count how many we flipped. */
    uint64_t injected = 0;
    for (int i = 0; i < 7000; i++) {
        uint8_t bit = prbs_next_bit(&tx);
        if ((i % 7) == 0) {
            bit ^= 1u;
            injected++;
        }
        prbs_check_bit(&chk, bit);
    }
    TEST_ASSERT_EQUAL_UINT64(7000u, chk.total);
    TEST_ASSERT_EQUAL_UINT64(injected, chk.errors);
}

static void test_checker_mismatched_seed_high_error_rate(void)
{
    /* Different seed but same poly: streams decorrelate -> ~50% errors. */
    prbs_t tx;
    prbs_check_t chk;
    prbs_init(&tx, PRBS15, 0x0001u);
    prbs_check_init(&chk, PRBS15, 0x7FFFu);

    for (int i = 0; i < 4000; i++) {
        prbs_check_bit(&chk, prbs_next_bit(&tx));
    }
    /* Expect roughly half errors; assert a wide band to avoid flakiness. */
    TEST_ASSERT_TRUE(chk.errors > 1500u);
    TEST_ASSERT_TRUE(chk.errors < 2500u);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_prbs9_period_value);
    RUN_TEST(test_prbs15_period_value);
    RUN_TEST(test_zero_seed_is_forced_nonzero);
    RUN_TEST(test_prbs9_returns_after_full_period);
    RUN_TEST(test_prbs9_ones_balance_over_period);
    RUN_TEST(test_next_bits_matches_single);
    RUN_TEST(test_next_bits_null_is_safe);
    RUN_TEST(test_checker_clean_stream_zero_errors);
    RUN_TEST(test_checker_counts_injected_errors);
    RUN_TEST(test_checker_mismatched_seed_high_error_rate);
    return UNITY_END();
}
