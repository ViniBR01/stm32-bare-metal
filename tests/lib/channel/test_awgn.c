#include "unity.h"
#include "awgn.h"
#include "bpsk.h"
#include "prbs.h"
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

/* --- PRNG determinism ---------------------------------------------------- */

static void test_prng_same_seed_same_stream(void)
{
    awgn_prng_t a, b;
    awgn_prng_seed(&a, 12345u);
    awgn_prng_seed(&b, 12345u);
    for (int i = 0; i < 1000; i++) {
        TEST_ASSERT_EQUAL_UINT32(awgn_prng_u32(&a), awgn_prng_u32(&b));
    }
}

static void test_prng_different_seed_diverges(void)
{
    awgn_prng_t a, b;
    awgn_prng_seed(&a, 1u);
    awgn_prng_seed(&b, 2u);
    int differences = 0;
    for (int i = 0; i < 100; i++) {
        if (awgn_prng_u32(&a) != awgn_prng_u32(&b)) {
            differences++;
        }
    }
    TEST_ASSERT_TRUE(differences > 90);
}

static void test_reseed_resets_gauss_cache(void)
{
    /* A reseed must yield an identical Gaussian stream (no leftover spare). */
    awgn_prng_t a;
    awgn_prng_seed(&a, 777u);
    float first_run[8];
    for (int i = 0; i < 8; i++) {
        first_run[i] = awgn_prng_gauss(&a);
    }

    awgn_prng_seed(&a, 777u);
    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_EQUAL_FLOAT(first_run[i], awgn_prng_gauss(&a));
    }
}

/* --- Gaussian statistics ------------------------------------------------- */

static void test_gauss_mean_and_variance(void)
{
    awgn_prng_t rng;
    awgn_prng_seed(&rng, 42u);

    const int N = 200000;
    double sum = 0.0, sumsq = 0.0;
    for (int i = 0; i < N; i++) {
        float g = awgn_prng_gauss(&rng);
        sum   += g;
        sumsq += (double)g * g;
    }
    double mean = sum / N;
    double var  = sumsq / N - mean * mean;

    TEST_ASSERT_TRUE(fabs(mean) < 0.02);          /* ~0 */
    TEST_ASSERT_TRUE(fabs(var - 1.0) < 0.03);     /* ~1 */
}

/* --- sigma mapping ------------------------------------------------------- */

static void test_sigma_matches_formula(void)
{
    /*
     * Hand-computed expectations (not a re-derivation of the function under
     * test): sigma = sqrt(1 / (2 * 10^(dB/10))).
     *   0 dB  -> sqrt(1/2)            = 0.70710678
     *   3 dB  -> sqrt(1/(2*1.9952623)) = 0.50059306
     *   10 dB -> sqrt(1/20)           = 0.22360680
     */
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.70710678f, channel_awgn_sigma(0.0f));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.50059306f, channel_awgn_sigma(3.0f));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.22360680f, channel_awgn_sigma(10.0f));
}

static void test_theory_ber_known_points(void)
{
    /* Reference BPSK BER values (0.5*erfc(sqrt(Eb/N0_lin))). */
    TEST_ASSERT_DOUBLE_WITHIN(1e-4, 0.0786496, channel_awgn_theory_ber(0.0f));
    TEST_ASSERT_DOUBLE_WITHIN(1e-4, 0.0375061, channel_awgn_theory_ber(2.0f));
    TEST_ASSERT_DOUBLE_WITHIN(1e-5, 0.0023883, channel_awgn_theory_ber(6.0f));
}

/* --- headline: end-to-end BER tracks theory ------------------------------ */

/*
 * Run PRBS -> BPSK map -> AWGN -> slice -> BER and compare the measured BER to
 * the closed-form BPSK curve. With N bits the standard error of the measured
 * BER is ~sqrt(p(1-p)/N); we use a generous absolute+relative band so the test
 * is tight enough to catch a wrong scale factor but not flaky.
 */
static double measure_ber(float ebn0_db, int nbits, uint32_t seed)
{
    prbs_t tx;
    prbs_check_t chk;
    awgn_prng_t  rng;
    prbs_init(&tx, PRBS15, 0xBEEFu);
    prbs_check_init(&chk, PRBS15, 0xBEEFu);
    awgn_prng_seed(&rng, seed);

    for (int i = 0; i < nbits; i++) {
        uint8_t bit = prbs_next_bit(&tx);
        q15_t   sym = bpsk_map(bit);
        channel_awgn_apply(&sym, 1, ebn0_db, &rng);
        prbs_check_bit(&chk, bpsk_slice(sym));
    }
    return (double)chk.errors / (double)chk.total;
}

static void test_ber_tracks_theory_curve(void)
{
    const int N = 400000;
    const float points[] = {0.0f, 2.0f, 4.0f, 6.0f};

    for (unsigned k = 0; k < sizeof(points)/sizeof(points[0]); k++) {
        float db = points[k];
        double measured = measure_ber(db, N, 0xC0FFEEu + k);
        double theory   = channel_awgn_theory_ber(db);

        /* Band: max(absolute floor, relative) to handle small BER at high SNR. */
        double tol = theory * 0.20 + 5.0e-4;
        TEST_ASSERT_DOUBLE_WITHIN(tol, theory, measured);
    }
}

static void test_ber_deterministic_for_seed(void)
{
    double a = measure_ber(4.0f, 50000, 0x5151u);
    double b = measure_ber(4.0f, 50000, 0x5151u);
    TEST_ASSERT_EQUAL_DOUBLE(a, b);
}

static void test_apply_null_args_safe(void)
{
    awgn_prng_t rng;
    awgn_prng_seed(&rng, 1u);
    q15_t s = 0;
    channel_awgn_apply(NULL, 4, 3.0f, &rng);
    channel_awgn_apply(&s, 1, 3.0f, NULL);
    TEST_PASS();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_prng_same_seed_same_stream);
    RUN_TEST(test_prng_different_seed_diverges);
    RUN_TEST(test_reseed_resets_gauss_cache);
    RUN_TEST(test_gauss_mean_and_variance);
    RUN_TEST(test_sigma_matches_formula);
    RUN_TEST(test_theory_ber_known_points);
    RUN_TEST(test_ber_tracks_theory_curve);
    RUN_TEST(test_ber_deterministic_for_seed);
    RUN_TEST(test_apply_null_args_safe);
    return UNITY_END();
}
