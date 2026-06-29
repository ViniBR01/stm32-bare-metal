#ifndef LIB_SYNC_AGC_H
#define LIB_SYNC_AGC_H

#include "complexq15.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Automatic gain control (Plan 002 sub-track B0.5). See
 * docs/wiki/plans/002-dsp-baseband/software-modem.md.
 *
 * A slow scalar-gain loop that drives the received signal amplitude toward a
 * reference level so the downstream timing and phase loops see a consistent
 * scale regardless of channel attenuation. The gain and its update run in
 * float (scalar control-loop math, explicitly sanctioned); only the signal it
 * scales stays q15.
 *
 * Update law (LMS-style on the magnitude):
 *     gain <- gain + mu * (ref - |x_out|)
 * where x_out is the gain-scaled output. |x| uses the q15 magnitude on the
 * unit scale (1.0 == 32768). A small mu makes the loop slow and stable.
 */

typedef struct {
    float gain;   /* current linear gain                                  */
    float ref;    /* target output magnitude on the unit scale (0..1)     */
    float mu;     /* loop step size (small -> slow, stable)               */
} agc_t;

/* Initialise: target magnitude ref, step mu, starting gain gain0 (>0). */
void agc_init(agc_t *a, float ref, float mu, float gain0);

/*
 * Scale one complex sample by the current gain, update the gain from the
 * scaled magnitude, and return the scaled sample (q15, saturating).
 */
cq15_t agc_apply(agc_t *a, cq15_t x);

#ifdef __cplusplus
}
#endif

#endif /* LIB_SYNC_AGC_H */
