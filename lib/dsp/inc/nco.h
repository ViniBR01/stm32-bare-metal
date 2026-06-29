#ifndef LIB_DSP_NCO_H
#define LIB_DSP_NCO_H

#include <stdint.h>
#include <stddef.h>
#include "complexq15.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Numerically-controlled oscillator (Plan 002 sub-track B0.5). See
 * docs/wiki/plans/002-dsp-baseband/software-modem.md.
 *
 * Generates the complex phasor e^{j*phase} one sample at a time and advances
 * the phase by a fixed increment. Phase and increment are uint32 over the full
 * circle (2^32 == 2*pi), so the accumulator wraps exactly on overflow with no
 * conditional. Used both to inject carrier-frequency / phase offset in the
 * impairment channel and to de-rotate in the Costas recovery loop, where
 * nco_set_incr() retunes the oscillator each symbol from the loop filter.
 *
 * The phasor itself is q15 (via the sincos LUT), so the NCO output multiplies
 * cleanly into the cq15 sample path; only the control loop that drives
 * nco_set_incr() runs in float.
 */

typedef struct {
    uint32_t phase;   /* current phase, full circle == 2^32        */
    uint32_t incr;    /* per-sample phase increment                */
} nco_t;

/* Initialise with a starting phase and per-sample increment. */
void nco_init(nco_t *o, uint32_t phase0, uint32_t incr);

/* Retune the per-sample increment (e.g. from a phase-recovery loop filter). */
void nco_set_incr(nco_t *o, uint32_t incr);

/* Return e^{j*phase} as a unit-magnitude cq15, then advance the phase. */
cq15_t nco_step(nco_t *o);

#ifdef __cplusplus
}
#endif

#endif /* LIB_DSP_NCO_H */
