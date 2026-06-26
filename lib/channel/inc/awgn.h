#ifndef LIB_CHANNEL_AWGN_H
#define LIB_CHANNEL_AWGN_H

#include <stdint.h>
#include <stddef.h>
#include "fixed.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Software AWGN (additive white Gaussian noise) channel for the software modem
 * (Plan 002 sub-track B0). See docs/wiki/plans/002-dsp-baseband/software-modem.md.
 *
 * This is the "emulated wireless channel": instead of a real RF or analog
 * link, the channel is a function that adds Gaussian noise to the transmitted
 * samples. Everything is deterministic given a seed, so host and HIL runs are
 * exactly reproducible.
 *
 *
 * Noise scaling for BPSK
 * ----------------------
 * BPSK symbols are +/-1.0, so the symbol energy is Es = 1, and with one bit per
 * symbol Eb = Es = 1. The real-valued baseband model is
 *
 *     r = s + n,    n ~ N(0, sigma^2),   N0/2 = sigma^2
 *
 * so Eb/N0 (linear) = Es / N0 = 1 / (2 * sigma^2), giving
 *
 *     sigma = sqrt( 1 / (2 * (Eb/N0)_linear) ),   (Eb/N0)_linear = 10^(dB/10).
 *
 * The closed-form bit error rate this model should reproduce is
 *
 *     BER = Q( sqrt(2 * (Eb/N0)_linear) ) = 0.5 * erfc( sqrt((Eb/N0)_linear) ).
 *
 * channel_awgn_theory_ber() returns exactly that, so tests can compare the
 * measured BER against theory. Symbols live on the q15 scale (+/-1.0 == +/-Q15_ONE),
 * so the noise standard deviation is applied at the same scale and the noisy
 * sum is saturated back into q15.
 */

/* Deterministic PRNG (xorshift128). Same seed -> same stream, on host and target. */
typedef struct {
    uint32_t s[4];
    float    gauss_spare;   /* cached Box-Muller partner sample           */
    uint8_t  have_spare;    /* 1 if gauss_spare holds an unused sample     */
} awgn_prng_t;

/* Seed the PRNG. Any 32-bit seed is accepted; internally expanded to 4 words. */
void awgn_prng_seed(awgn_prng_t *rng, uint32_t seed);

/* Uniform 32-bit draw. */
uint32_t awgn_prng_u32(awgn_prng_t *rng);

/*
 * One standard-normal sample (mean 0, variance 1) via the Box-Muller transform.
 * Box-Muller produces pairs; this returns one per call and caches the partner.
 */
float awgn_prng_gauss(awgn_prng_t *rng);

/* Eb/N0 in dB -> noise standard deviation on the unit (+/-1.0) symbol scale. */
float channel_awgn_sigma(float ebn0_db);

/* Closed-form BPSK BER for a given Eb/N0 in dB: 0.5 * erfc(sqrt(Eb/N0_linear)). */
double channel_awgn_theory_ber(float ebn0_db);

/*
 * Add AWGN to a block of q15 samples in place. The noise standard deviation is
 * derived from ebn0_db via channel_awgn_sigma() and applied on the q15 scale;
 * each noisy sample is saturated into the q15 range.
 */
void channel_awgn_apply(q15_t *samples, size_t n, float ebn0_db, awgn_prng_t *rng);

#ifdef __cplusplus
}
#endif

#endif /* LIB_CHANNEL_AWGN_H */
