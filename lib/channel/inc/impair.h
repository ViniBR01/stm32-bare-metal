#ifndef LIB_CHANNEL_IMPAIR_H
#define LIB_CHANNEL_IMPAIR_H

#include <stdint.h>
#include <stddef.h>
#include "fixed.h"
#include "complexq15.h"
#include "nco.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Channel impairments for the software modem (Plan 002 sub-track B0.5). See
 * docs/wiki/plans/002-dsp-baseband/software-modem.md.
 *
 * Sits between the (real) RRC transmit shaper and the AWGN channel, turning the
 * clean real waveform into the complex baseband a real link would deliver:
 *
 *   1. fractional-sample timing offset  (linear interpolation, sub-sample delay)
 *   2. carrier-frequency offset (CFO)    (per-sample phasor rotation)
 *   3. static phase offset               (constant rotation)
 *
 * CFO and phase offset are produced by one NCO: the static phase is its initial
 * phase, the CFO its per-sample increment, so a single complex multiply applies
 * both. The output is cq15 (the AWGN channel's cq15 variant then runs on it).
 *
 * Every field is "off" at its zero/identity value, so a default-zeroed config
 * passes the signal through as cq15_from_real(in) with no rotation — which is
 * how the modem keeps its no-impairment path bit-exact.
 *
 * Pure C, no peripheral access: compiles on host and target.
 */

typedef struct {
    uint32_t cfo_incr;     /* per-sample phase increment (0 = no CFO)            */
    uint32_t phase0;       /* static phase offset, full circle 2^32 (0 = none)   */
    q15_t    timing_mu;    /* sub-sample delay in [0,1) as q15 (0 = none)        */
} channel_impair_cfg_t;

typedef struct {
    nco_t   nco;           /* CFO + phase rotator                                */
    q15_t   prev;          /* previous real input sample (interp history)        */
    uint8_t have_prev;     /* 0 until the first sample primes the interpolator   */
} channel_impair_state_t;

/* Initialise the impairment state from a config (seeds the NCO, clears history). */
void channel_impair_init(channel_impair_state_t *st, const channel_impair_cfg_t *cfg);

/*
 * Apply timing offset, CFO, and phase offset to n real input samples, writing
 * n complex outputs. in_real and out must hold n elements (out may not alias
 * in_real). State persists across calls so the chain can run block-by-block.
 */
void channel_impair_apply(channel_impair_state_t *st, const channel_impair_cfg_t *cfg,
                          const q15_t *in_real, cq15_t *out, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* LIB_CHANNEL_IMPAIR_H */
