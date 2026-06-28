#include "unity.h"
#include "rrc.h"
#include "bpsk.h"
#include "prbs.h"
#include "awgn.h"
#include "vectors/rrc_golden.h"
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

/* Shared default filters; (re)designed per test that needs a clean delay line. */
static rrc_t g_tx;
static rrc_t g_rx;

/* --- tap design ---------------------------------------------------------- */

static void test_design_rejects_bad_params(void)
{
    rrc_t f;
    TEST_ASSERT_EQUAL_UINT8(0, rrc_design(&f, -0.1f, 4, 8));   /* beta < 0   */
    TEST_ASSERT_EQUAL_UINT8(0, rrc_design(&f, 1.5f, 4, 8));    /* beta > 1   */
    TEST_ASSERT_EQUAL_UINT8(0, rrc_design(&f, 0.35f, 1, 8));   /* sps too lo */
    TEST_ASSERT_EQUAL_UINT8(0, rrc_design(&f, 0.35f, 9, 8));   /* sps too hi */
    TEST_ASSERT_EQUAL_UINT8(0, rrc_design(&f, 0.35f, 4, 1));   /* span too lo*/
    TEST_ASSERT_EQUAL_UINT8(0, rrc_design(&f, 0.35f, 4, 17));  /* span too hi*/
    TEST_ASSERT_EQUAL_UINT8(0, rrc_design(NULL, 0.35f, 4, 8)); /* null       */
}

static void test_design_tap_count_and_symmetry(void)
{
    rrc_t f;
    uint8_t n = rrc_design(&f, RRC_GOLDEN_BETA, RRC_GOLDEN_SPS, RRC_GOLDEN_SPAN);
    TEST_ASSERT_EQUAL_UINT8(RRC_GOLDEN_NTAPS, n);

    /* Linear-phase RRC: taps are symmetric about the centre. */
    for (uint8_t i = 0; i < n; i++) {
        TEST_ASSERT_EQUAL_INT16(f.taps[i], f.taps[n - 1u - i]);
    }
}

static void test_taps_match_golden_within_2lsb(void)
{
    rrc_t f;
    rrc_design(&f, RRC_GOLDEN_BETA, RRC_GOLDEN_SPS, RRC_GOLDEN_SPAN);
    for (uint8_t i = 0; i < RRC_GOLDEN_NTAPS; i++) {
        TEST_ASSERT_INT16_WITHIN(2, rrc_golden_taps[i], f.taps[i]);
    }
}

static void test_taps_unit_energy(void)
{
    rrc_t f;
    rrc_design(&f, RRC_GOLDEN_BETA, RRC_GOLDEN_SPS, RRC_GOLDEN_SPAN);
    double energy = 0.0;
    for (uint8_t i = 0; i < f.ntaps; i++) {
        double v = (double)f.taps[i] / 32768.0;
        energy += v * v;
    }
    /* Quantisation perturbs the float unit-energy slightly; within 0.5%. */
    TEST_ASSERT_DOUBLE_WITHIN(0.005, 1.0, energy);
}

/* --- Nyquist ISI-free cascade -------------------------------------------- */

/*
 * Drive a single +1.0 symbol through TX-shape then RX-match. The cascade is a
 * raised cosine: its peak sits at the chain delay and every other symbol-spaced
 * sample (delay +/- k*sps) must be ~zero. That zero-ISI property is what lets
 * neighbouring symbols not interfere at the decision instant.
 */
static void test_cascade_is_isi_free_at_symbol_instants(void)
{
    rrc_design(&g_tx, RRC_GOLDEN_BETA, RRC_GOLDEN_SPS, RRC_GOLDEN_SPAN);
    rrc_design(&g_rx, RRC_GOLDEN_BETA, RRC_GOLDEN_SPS, RRC_GOLDEN_SPAN);

    uint8_t sps = g_tx.sps;
    /* Enough symbols that the impulse fully traverses both filters. */
    const size_t nsyms = 2u * RRC_GOLDEN_SPAN + 4u;
    q15_t syms[64];
    q15_t tx[64 * RRC_MAX_SPS];
    q15_t rx[64 * RRC_MAX_SPS];

    for (size_t i = 0; i < nsyms; i++) {
        syms[i] = 0;
    }
    syms[0] = BPSK_SYM_HI;   /* lone +1.0 impulse */

    rrc_tx_shape(&g_tx, syms, nsyms, tx);
    rrc_rx_match(&g_rx, tx, nsyms * sps, rx);

    size_t delay = rrc_chain_delay(&g_tx);   /* peak index for symbol 0 */
    int32_t peak = rx[delay];
    TEST_ASSERT_TRUE(peak > 20000);          /* near +1.0 (well-formed peak) */

    /*
     * Symbol-spaced neighbours of the peak must be a tiny fraction of it. With
     * q15 taps the residual ISI is a few hundred LSB at most; require each
     * neighbour below 3% of the peak.
     */
    int32_t bound = peak / 32;               /* ~3% */
    for (int k = 1; k <= RRC_GOLDEN_SPAN; k++) {
        size_t right = delay + (size_t)k * sps;
        if (right < nsyms * sps) {
            TEST_ASSERT_INT32_WITHIN(bound, 0, (int32_t)rx[right]);
        }
        if (delay >= (size_t)k * sps) {
            size_t left = delay - (size_t)k * sps;
            TEST_ASSERT_INT32_WITHIN(bound, 0, (int32_t)rx[left]);
        }
    }
}

/* --- end-to-end BER ------------------------------------------------------ */

/*
 * Full waveform chain: PRBS -> BPSK map -> TX shape -> [AWGN at sample rate]
 * -> RX matched filter -> decimate at symbol instants -> slice -> BER. The
 * transmit and receive filters carry independent delay lines; symbol k peaks
 * in the matched-filter stream at absolute sample index k*sps + chain_delay,
 * so we decimate there. The checker shares the PRBS seed with the transmitter
 * but is advanced only on decimated symbols, so it stays aligned once the
 * first payload symbol emerges. Returns errors and compared-bit count.
 */
static void run_chain(float ebn0_db, int payload_syms, uint32_t seed,
                      uint64_t *errors, uint64_t *nbits)
{
    rrc_design(&g_tx, RRC_GOLDEN_BETA, RRC_GOLDEN_SPS, RRC_GOLDEN_SPAN);
    rrc_design(&g_rx, RRC_GOLDEN_BETA, RRC_GOLDEN_SPS, RRC_GOLDEN_SPAN);

    prbs_t tx_bits;
    prbs_check_t chk;
    awgn_prng_t rng;
    prbs_init(&tx_bits, PRBS15, 0xBEEFu);
    prbs_check_init(&chk, PRBS15, 0xBEEFu);
    awgn_prng_seed(&rng, seed);

    uint8_t sps = g_tx.sps;
    size_t delay_samples = rrc_chain_delay(&g_tx);

    /*
     * Pad with trailing zero symbols (one chain delay plus one) so the last
     * payload symbol flushes through both filters and reaches the decimator.
     */
    size_t total_syms = (size_t)payload_syms + delay_samples / sps + 1u;

    /* One symbol -> sps samples; process block-by-block to bound memory. */
    static q15_t txwin[RRC_MAX_SPS];

    size_t sample_idx = 0;             /* absolute matched-filter sample index */
    size_t next_peak = delay_samples;  /* sample index of the next symbol peak */
    size_t produced = 0;               /* decimated symbols checked so far     */

    for (size_t k = 0; k < total_syms; k++) {
        q15_t sym = (k < (size_t)payload_syms)
                        ? bpsk_map(prbs_next_bit(&tx_bits))
                        : 0;   /* flush tail */

        /* TX: symbol then sps-1 zero-stuffed samples, all shaped. */
        txwin[0] = rrc_push(&g_tx, sym);
        for (uint8_t p = 1; p < sps; p++) {
            txwin[p] = rrc_push(&g_tx, 0);
        }

        /* Channel at sample rate (skipped when ebn0_db is the noiseless flag). */
        if (ebn0_db < 1e30f) {
            channel_awgn_apply(txwin, sps, ebn0_db, &rng);
        }

        /* RX matched filter; decimate + check whenever a symbol peak lands. */
        for (uint8_t p = 0; p < sps; p++) {
            q15_t y = rrc_push(&g_rx, txwin[p]);
            if (sample_idx == next_peak && produced < (size_t)payload_syms) {
                prbs_check_bit(&chk, bpsk_slice(y));
                next_peak += sps;
                produced++;
            }
            sample_idx++;
        }
    }

    *errors = chk.errors;
    *nbits = chk.total;
}

static void test_noiseless_ber_is_zero(void)
{
    uint64_t errors = 0, nbits = 0;
    run_chain(1e30f, 4000, 1u, &errors, &nbits);
    TEST_ASSERT_TRUE(nbits >= 3990u);     /* essentially all payload compared */
    TEST_ASSERT_EQUAL_UINT64(0u, errors);
}

static void test_ber_with_noise_tracks_theory(void)
{
    const float points[] = {0.0f, 2.0f, 4.0f, 6.0f};
    const int payload = 60000;

    for (unsigned i = 0; i < sizeof(points) / sizeof(points[0]); i++) {
        uint64_t errors = 0, nbits = 0;
        run_chain(points[i], payload, 0xC0FFEEu + i, &errors, &nbits);
        double measured = (double)errors / (double)nbits;
        double theory = channel_awgn_theory_ber(points[i]);
        /* Matched filter is lossless, so the symbol-level band still applies. */
        double tol = theory * 0.20 + 1.5e-3;
        TEST_ASSERT_DOUBLE_WITHIN(tol, theory, measured);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_design_rejects_bad_params);
    RUN_TEST(test_design_tap_count_and_symmetry);
    RUN_TEST(test_taps_match_golden_within_2lsb);
    RUN_TEST(test_taps_unit_energy);
    RUN_TEST(test_cascade_is_isi_free_at_symbol_instants);
    RUN_TEST(test_noiseless_ber_is_zero);
    RUN_TEST(test_ber_with_noise_tracks_theory);
    return UNITY_END();
}
