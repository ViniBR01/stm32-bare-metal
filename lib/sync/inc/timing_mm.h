#ifndef LIB_SYNC_TIMING_MM_H
#define LIB_SYNC_TIMING_MM_H

#include <stdint.h>
#include "complexq15.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Mueller & Muller symbol-timing recovery (Plan 002 sub-track B0.5). See
 * docs/wiki/plans/002-dsp-baseband/software-modem.md.
 *
 * Consumes matched-filter output samples at SPS samples/symbol and emits one
 * interpolated symbol per symbol period, locked to the true symbol instant
 * even under a fractional-sample timing offset. The classic M&M timing-error
 * detector for BPSK is
 *
 *     e = a[k-1] * y_I[k]  -  a[k] * y_I[k-1]
 *
 * where y[k] is the current interpolated symbol, y[k-1] the previous, and a[.]
 * the hard decision sign(y_I). The loop integrates e into a fractional sample
 * index mu in [0,1); when mu wraps past 1 the loop skips a sample (and below 0,
 * repeats one), keeping the interpolation phase aligned to the symbol clock.
 *
 * Interpolation is linear between adjacent samples (v1; a Farrow cubic is a
 * documented future improvement). The loop state and gain are float (scalar
 * control math); the emitted symbol stays cq15.
 */

typedef struct {
    uint8_t sps;          /* input samples per symbol                        */
    float   loop_gain;    /* TED -> timing update gain (small = slow, stable)*/
    float   mu;           /* last timing-error-detector output (inspection)  */

    cq15_t  last;         /* previous input sample (for linear interp)       */
    uint8_t have_last;    /* 1 once last is primed                           */

    float   pos;          /* running integer index of the current sample     */
    float   next_t;       /* fractional sample position of next symbol instant*/

    cq15_t  prev_sym;     /* previous emitted symbol y[k-1]                  */
    q15_t   prev_dec;     /* previous hard decision a[k-1] (+1/-1 q15)       */
    uint8_t have_prev;    /* 1 once prev_sym/prev_dec are valid              */
} timing_mm_t;

/* Initialise for sps samples/symbol with the given loop gain. */
void timing_mm_init(timing_mm_t *t, uint8_t sps, float loop_gain);

/*
 * Feed one matched-filter sample. Returns 1 and writes *out when a symbol is
 * produced this call (about once every sps calls, modulated by the timing
 * loop); returns 0 otherwise.
 */
int timing_mm_push(timing_mm_t *t, cq15_t sample, cq15_t *out);

#ifdef __cplusplus
}
#endif

#endif /* LIB_SYNC_TIMING_MM_H */
