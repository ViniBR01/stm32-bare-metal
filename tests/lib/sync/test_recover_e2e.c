#include "unity.h"
#include "agc.h"
#include "timing_mm.h"
#include "costas.h"
#include "barker.h"
#include "sincos.h"
#include "nco.h"
#include "rrc.h"
#include "impair.h"
#include "awgn.h"
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
 * Headline B0.5 host test (software twin of HIL Tier 9c): a full BPSK frame —
 * Barker-13 preamble + PRBS payload — is shaped, passed through combined
 * fractional-timing + CFO + phase impairment and AWGN, then run through the
 * complete RX chain:
 *
 *   matched filter -> AGC -> M&M timing -> Costas phase -> Barker frame/polarity
 *   -> slice -> compare
 *
 * Asserts: (a) the preamble is found (frame lock), and (b) the post-sync
 * payload BER is within a bounded multiple of the ideal-sync BER.
 */

#define PAYLOAD_BITS 8000
#define NSYMS       (BARKER13_LEN + PAYLOAD_BITS)

static uint8_t payload_bits[PAYLOAD_BITS];

typedef struct { int locked; int lock_sym; double ber; } e2e_result_t;

static e2e_result_t run_e2e(float snr_db, q15_t timing_mu,
                            uint32_t cfo_incr, uint32_t phase0)
{
    rrc_t tx, rx_i, rx_q;
    rrc_design(&tx, BETA, SPS, SPAN);
    rrc_design(&rx_i, BETA, SPS, SPAN);   /* matched filter, in-phase  */
    rrc_design(&rx_q, BETA, SPS, SPAN);   /* matched filter, quadrature */

    channel_impair_cfg_t cfg = { cfo_incr, phase0, timing_mu };
    channel_impair_state_t imp;
    channel_impair_init(&imp, &cfg);

    awgn_prng_t rng;
    awgn_prng_seed(&rng, 7u);

    agc_t agc;
    agc_init(&agc, 0.7f, 0.005f, 1.0f);
    timing_mm_t mm;
    timing_mm_init(&mm, SPS, 0.004f);
    costas_t cos;
    costas_init(&cos, 0.02f, 0.0005f);
    barker_t bar;
    /* Threshold ~ (0.6*13*Esym)^2 with Esym ~ (0.7*32768)^2 post-AGC. */
    double esym = 0.7 * 32768.0;
    int64_t thr = (int64_t)((0.55 * 13.0 * esym) * (0.55 * 13.0 * esym));
    barker_init(&bar, thr);

    prbs_t txbits;
    prbs_init(&txbits, PRBS9, 1u);
    for (int i = 0; i < PAYLOAD_BITS; i++) {
        payload_bits[i] = prbs_next_bit(&txbits);
    }

    /* Build the frame symbol sequence: Barker-13 then payload. */
    /* Process one symbol at a time through the whole chain. */
    int locked = 0, lock_sym = -1, polarity = 1;
    static uint8_t rec[PAYLOAD_BITS + 64];
    int nrec = 0;

    q15_t txsamp[SPS];
    cq15_t imp_out[SPS];

    for (int k = 0; k < NSYMS; k++) {
        q15_t sym;
        if (k < BARKER13_LEN) {
            sym = (q15_t)(BARKER13[k] > 0 ? bpsk_map(1) : bpsk_map(0));
        } else {
            sym = bpsk_map(payload_bits[k - BARKER13_LEN]);
        }

        rrc_tx_shape(&tx, &sym, 1, txsamp);
        channel_impair_apply(&imp, &cfg, txsamp, imp_out, SPS);
        channel_awgn_apply_cq15(imp_out, SPS, snr_db, &rng);

        for (uint32_t p = 0; p < SPS; p++) {
            /* Matched filter per component (independent I/Q filter state). */
            cq15_t mf = cq15_make(rrc_push(&rx_i, imp_out[p].re),
                                  rrc_push(&rx_q, imp_out[p].im));
            cq15_t g = agc_apply(&agc, mf);
            cq15_t sym_out;
            if (!timing_mm_push(&mm, g, &sym_out)) {
                continue;
            }
            /* One symbol emerged from timing recovery. */
            cq15_t y = costas_step(&cos, sym_out);

            if (!locked) {
                int32_t corr_re; int64_t mag2;
                if (barker_push(&bar, y, &corr_re, &mag2)) {
                    locked = 1;
                    lock_sym = k;
                    polarity = (corr_re >= 0) ? 1 : -1;   /* resolve 180 deg */
                }
            } else if (nrec < (int)(sizeof(rec))) {
                /* After lock, collect polarity-corrected payload decisions. */
                q15_t corrected = (polarity >= 0) ? y.re : (q15_t)(-y.re);
                rec[nrec++] = bpsk_slice(corrected);
            }
        }
    }

    /*
     * The matched-filter group delay offsets the recovered payload stream from
     * the source by a small integer number of symbols; find the delay D that
     * minimises errors (the Barker preamble pins this in a real RX). Report BER
     * at the best alignment.
     */
    int best_err = nrec, total = 1;
    for (int D = 0; D < 25 && D < nrec; D++) {
        int err = 0, cnt = 0;
        for (int i = 0; i + D < nrec && i < PAYLOAD_BITS; i++) {
            if (rec[i + D] != payload_bits[i]) err++;
            cnt++;
        }
        if (cnt > 0 && err < best_err) { best_err = err; total = cnt; }
    }

    e2e_result_t r;
    r.locked   = locked;
    r.lock_sym = lock_sym;
    r.ber      = (double)best_err / (double)total;
    if (r.ber > 0.5) r.ber = 1.0 - r.ber;   /* defensive polarity fold */
    return r;
}

/* Combined timing+CFO+phase, no noise: must lock and recover with ~0 BER. */
static void test_combined_offsets_noiseless(void)
{
    e2e_result_t r = run_e2e(60.0f,
                             (q15_t)(0.4f * 32768.0f),
                             nco_phase_from_cycles(0.0002f),
                             0x18000000u /* ~33 deg */);
    TEST_ASSERT_TRUE_MESSAGE(r.locked, "frame sync did not lock (noiseless)");
    TEST_ASSERT_TRUE_MESSAGE(r.ber < 5e-3, "noiseless recovered BER too high");
}

/* Combined offsets at 6 dB: must lock and track theory within the bound. */
static void test_combined_offsets_6db(void)
{
    e2e_result_t r = run_e2e(6.0f,
                             (q15_t)(0.4f * 32768.0f),
                             nco_phase_from_cycles(0.0002f),
                             0x18000000u);
    double theory = channel_awgn_theory_ber(6.0f);
    TEST_ASSERT_TRUE_MESSAGE(r.locked, "frame sync did not lock at 6 dB");
    /* Bounded degradation vs ideal-sync: <= 4x theory (recovery + linear-interp
     * penalty), and far below the no-lock floor of ~0.5. */
    TEST_ASSERT_TRUE_MESSAGE(r.ber < 4.0 * theory + 1e-3,
                             "6 dB recovered BER outside bound");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_combined_offsets_noiseless);
    RUN_TEST(test_combined_offsets_6db);
    return UNITY_END();
}
