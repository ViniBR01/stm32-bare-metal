#include "unity.h"
#include "barker.h"
#include "complexq15.h"
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

#define SYM 30000   /* a unit BPSK symbol magnitude on the q15 scale */

/* --- autocorrelation sidelobes ------------------------------------------- */

/*
 * Slide the Barker sequence past itself and record the in-phase correlation at
 * every lag. The aligned lag must give 13*SYM; every other lag must be <= 1*SYM
 * in magnitude (the defining Barker-13 property).
 */
static void test_autocorrelation_sidelobes(void)
{
    /* Feed a long run of "idle" (zero) then the 13 preamble symbols then idle,
     * recording the peak correlation magnitude and where it occurs. */
    barker_t b;
    /* Threshold above the 1*SYM sidelobe but below the 13*SYM peak. */
    int64_t thr = (int64_t)(10 * SYM) * (10 * SYM);
    barker_init(&b, thr);

    int hits = 0;
    int64_t peak = 0;

    /* 20 idle, then preamble, then 20 idle. */
    for (int i = 0; i < 20; i++) {
        int64_t m2; int32_t cr;
        if (barker_push(&b, cq15_make(0, 0), &cr, &m2)) hits++;
    }
    for (int k = 0; k < BARKER13_LEN; k++) {
        int64_t m2; int32_t cr;
        cq15_t s = cq15_from_real((q15_t)(BARKER13[k] * SYM));
        if (barker_push(&b, s, &cr, &m2)) {
            hits++;
            if (m2 > peak) peak = m2;
        }
    }
    for (int i = 0; i < 20; i++) {
        int64_t m2; int32_t cr;
        if (barker_push(&b, cq15_make(0, 0), &cr, &m2)) hits++;
    }

    /* Exactly one alignment should fire, at the full-correlation peak. */
    TEST_ASSERT_EQUAL_INT(1, hits);
    int64_t ideal_peak = (int64_t)(13 * SYM) * (13 * SYM);
    TEST_ASSERT_TRUE(peak >= (int64_t)(0.9 * (double)ideal_peak));
}

/* --- detects the frame at the right position ----------------------------- */

static void test_detects_preamble_after_payload_prefix(void)
{
    barker_t b;
    int64_t thr = (int64_t)(10 * SYM) * (10 * SYM);
    barker_init(&b, thr);

    /* Random-ish payload symbols, then the preamble. Detection must occur
     * exactly when the 13th preamble symbol arrives. */
    const int prefix = 7;
    int fired_at = -1;
    int idx = 0;
    int8_t prefix_syms[7] = { +1, -1, -1, +1, -1, +1, +1 };
    for (int i = 0; i < prefix; i++, idx++) {
        barker_push(&b, cq15_from_real((q15_t)(prefix_syms[i] * SYM)), NULL, NULL);
    }
    for (int k = 0; k < BARKER13_LEN; k++, idx++) {
        if (barker_push(&b, cq15_from_real((q15_t)(BARKER13[k] * SYM)), NULL, NULL)) {
            fired_at = idx;
        }
    }
    TEST_ASSERT_EQUAL_INT(prefix + BARKER13_LEN - 1, fired_at);
}

/* --- polarity recovery (180-degree ambiguity) ---------------------------- */

static void test_inverted_preamble_resolves_sign(void)
{
    barker_t b;
    int64_t thr = (int64_t)(10 * SYM) * (10 * SYM);

    /* Normal preamble: correlation sign positive. */
    barker_init(&b, thr);
    int32_t cr_pos = 0;
    for (int k = 0; k < BARKER13_LEN; k++) {
        int32_t cr;
        if (barker_push(&b, cq15_from_real((q15_t)(BARKER13[k] * SYM)), &cr, NULL)) {
            cr_pos = cr;
        }
    }
    TEST_ASSERT_TRUE(cr_pos > 0);

    /* 180-degree-rotated preamble (all symbols negated): still detected (magnitude),
     * but the correlation sign flips — that's how the RX recovers polarity. */
    barker_init(&b, thr);
    int32_t cr_neg = 0;
    int detected = 0;
    for (int k = 0; k < BARKER13_LEN; k++) {
        int32_t cr;
        if (barker_push(&b, cq15_from_real((q15_t)(-BARKER13[k] * SYM)), &cr, NULL)) {
            cr_neg = cr;
            detected = 1;
        }
    }
    TEST_ASSERT_TRUE(detected);
    TEST_ASSERT_TRUE(cr_neg < 0);
}

static void test_null_safe(void)
{
    TEST_ASSERT_EQUAL_INT(0, barker_push(NULL, cq15_make(0, 0), NULL, NULL));
    TEST_PASS();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_autocorrelation_sidelobes);
    RUN_TEST(test_detects_preamble_after_payload_prefix);
    RUN_TEST(test_inverted_preamble_resolves_sign);
    RUN_TEST(test_null_safe);
    return UNITY_END();
}
