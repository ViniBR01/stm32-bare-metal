#include "unity.h"
#include "timing_mm.h"
#include "rrc.h"
#include "impair.h"
#include "bpsk.h"
#include "prbs.h"
#include "complexq15.h"
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

#define SPS  4u
#define BETA 0.35f
#define SPAN 8u

/*
 * Drive a shaped BPSK stream with a fixed fractional-sample timing offset
 * through the matched filter and M&M recovery, and check that after the loop
 * locks the recovered hard decisions match the transmitted bits (BER -> 0,
 * noiseless). Timing recovery without a fixed decimation index is exactly the
 * job M&M does.
 */
static double run_timing(q15_t timing_mu, int nbits, int settle, float loop_gain)
{
    rrc_t tx, rx;
    rrc_design(&tx, BETA, SPS, SPAN);
    rrc_design(&rx, BETA, SPS, SPAN);

    channel_impair_cfg_t cfg = { 0u, 0u, timing_mu };   /* timing only */
    channel_impair_state_t imp;
    channel_impair_init(&imp, &cfg);

    timing_mm_t mm;
    timing_mm_init(&mm, SPS, loop_gain);

    prbs_t tx_bits;
    prbs_init(&tx_bits, PRBS9, 1u);

    /* Reference bit history so we can align recovered symbols to source bits. */
    static uint8_t src_bits[20000];
    for (int i = 0; i < nbits; i++) {
        src_bits[i] = prbs_next_bit(&tx_bits);
    }

    /* Process symbol-by-symbol: shape 1 symbol -> SPS samples -> impair ->
     * matched filter -> M&M. Collect recovered decisions. */
    static uint8_t rec[20000];
    int nrec = 0;
    q15_t txsamp[SPS];
    cq15_t imp_out[SPS];

    for (int k = 0; k < nbits && nrec < nbits; k++) {
        q15_t sym = bpsk_map(src_bits[k]);
        rrc_tx_shape(&tx, &sym, 1, txsamp);
        channel_impair_apply(&imp, &cfg, txsamp, imp_out, SPS);
        for (uint32_t p = 0; p < SPS; p++) {
            /* Matched filter on the real part (timing-only: im ~ 0). */
            q15_t mf = rrc_push(&rx, imp_out[p].re);
            cq15_t out;
            if (timing_mm_push(&mm, cq15_from_real(mf), &out)) {
                if (nrec < nbits) {
                    rec[nrec++] = bpsk_slice(out.re);
                }
            }
        }
    }

    /*
     * The TX/RX filter chain delays the recovered stream by an integer number
     * of symbols (rec[i+D] corresponds to src[i]); find the delay D that
     * minimises errors over the settled region, then report BER on that tail.
     * (A real RX uses the Barker preamble to find D; here we only need to
     * confirm the timing loop recovers the symbols.)
     */
    int best_err = nrec;
    for (int D = 0; D < 40 && D < nrec; D++) {
        int err = 0, cnt = 0;
        for (int i = settle; i + D < nrec && i < nbits; i++) {
            if (rec[i + D] != src_bits[i]) err++;
            cnt++;
        }
        if (cnt > 0 && err < best_err) best_err = err;
    }
    int total = nrec - settle - 40;
    return (total > 0) ? (double)best_err / (double)total : 1.0;
}

static void test_timing_offset_quarter_sample(void)
{
    /* mu = 0.25 sample offset. */
    double ber = run_timing((q15_t)(0.25f * 32768.0f), 4000, 400, 0.004f);
    TEST_ASSERT_TRUE(ber < 1e-3);
}

static void test_timing_offset_half_sample(void)
{
    double ber = run_timing((q15_t)(0.5f * 32768.0f), 4000, 400, 0.004f);
    TEST_ASSERT_TRUE(ber < 1e-3);
}

static void test_no_offset_recovers_cleanly(void)
{
    double ber = run_timing(0, 4000, 400, 0.004f);
    TEST_ASSERT_TRUE(ber < 1e-3);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_offset_recovers_cleanly);
    RUN_TEST(test_timing_offset_quarter_sample);
    RUN_TEST(test_timing_offset_half_sample);
    return UNITY_END();
}
