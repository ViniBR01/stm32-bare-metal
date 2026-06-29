#ifndef LIB_SYNC_COSTAS_H
#define LIB_SYNC_COSTAS_H

#include "complexq15.h"
#include "nco.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Decision-directed Costas phase/frequency recovery (Plan 002 sub-track B0.5).
 * See docs/wiki/plans/002-dsp-baseband/software-modem.md.
 *
 * Tracks the residual carrier phase and frequency offset left after the coarse
 * channel model, de-rotating each symbol onto the real axis so the BPSK slicer
 * sees a clean +/-1.0 constellation. The phase detector for BPSK is
 *
 *     e = sign(y_I) * y_Q
 *
 * (decision-directed: the in-phase decision times the quadrature error). A
 * second-order proportional-integral (PI) loop filter feeds an NCO: the
 * integrator state tracks the residual CFO, the proportional path corrects
 * instantaneous phase. The NCO runs in the q15 sample path; the loop filter
 * (alpha, beta, freq) is float.
 *
 * Costas cannot resolve the BPSK 180-degree ambiguity (it locks equally to
 * +phase or +phase+pi); the Barker preamble sign resolves the global polarity.
 *
 * Gain guidance: the decision-directed error is noisy, so under AWGN the loop
 * must be slow or it self-noises into cycle slips. Conservative gains such as
 * alpha ~ 0.02, beta ~ 0.0005 hold lock at 6 dB while still pulling in a small
 * residual CFO; larger gains lock faster but only survive at high SNR. The
 * host tests exercise both regimes.
 */

typedef struct {
    nco_t nco;       /* de-rotation phasor (conjugated on apply)            */
    float freq;      /* integrator state: residual CFO (cycles/symbol)      */
    float alpha;     /* proportional gain                                   */
    float beta;      /* integral gain                                       */
} costas_t;

/* Initialise with PI gains alpha (proportional) and beta (integral). */
void costas_init(costas_t *c, float alpha, float beta);

/*
 * De-rotate one symbol by the current NCO phase, update the loop from the
 * decision-directed phase error, and return the phase-corrected symbol.
 */
cq15_t costas_step(costas_t *c, cq15_t sym);

#ifdef __cplusplus
}
#endif

#endif /* LIB_SYNC_COSTAS_H */
