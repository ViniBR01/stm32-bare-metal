#include "timing_mm.h"
#include "fixed.h"
#include <stddef.h>
#include <math.h>

void timing_mm_init(timing_mm_t *t, uint8_t sps, float loop_gain)
{
    if (t == NULL) {
        return;
    }
    t->sps       = (sps >= 2u) ? sps : 2u;
    t->loop_gain = loop_gain;
    t->mu        = 0.0f;
    t->last      = cq15_make(0, 0);
    t->have_last = 0u;
    /*
     * next_t is the (fractional) input-sample position of the next symbol
     * instant. Start one nominal symbol in so there is always a predecessor
     * sample to interpolate against; the integer symbol-index offset (filter
     * transient) is absorbed downstream by frame sync.
     */
    t->pos       = 0.0f;
    t->next_t    = (float)t->sps;
    t->prev_sym  = cq15_make(0, 0);
    t->prev_dec  = 0;
    t->have_prev = 0u;
}

/* Linear interpolation a + f*(b-a), f in [0,1], saturating to q15. */
static cq15_t lerp(cq15_t a, cq15_t b, float f)
{
    float re = (float)a.re + f * ((float)b.re - (float)a.re);
    float im = (float)a.im + f * ((float)b.im - (float)a.im);
    return cq15_make(q15_sat((q31_t)lrintf(re)), q15_sat((q31_t)lrintf(im)));
}

int timing_mm_push(timing_mm_t *t, cq15_t sample, cq15_t *out)
{
    if (t == NULL) {
        return 0;
    }

    int emitted = 0;
    float n = t->pos;   /* integer index of the current input sample */

    /*
     * Emit a symbol when the running symbol instant next_t falls in the open
     * interval (n-1, n] — i.e. between the previous input sample and this one.
     * Interpolate linearly at the fractional position and run the M&M loop.
     */
    if (t->have_last && t->next_t <= n && t->next_t > n - 1.0f) {
        float frac = t->next_t - (n - 1.0f);   /* in (0,1] toward current */
        cq15_t y = lerp(t->last, sample, frac);
        q15_t  yi = y.re;
        int    a  = (yi >= 0) ? 1 : -1;

        float ctrl = 0.0f;
        if (t->have_prev) {
            /*
             * M&M timing-error detector for BPSK (in-phase):
             *   e = a[k-1]*y_I[k] - a[k]*y_I[k-1]
             * Normalised by the q15 unit scale so loop_gain is O(1). The error
             * nudges the next symbol instant (next_t) to slew the sampling phase
             * onto the symbol peak; the sign is set so the loop is negative-
             * feedback (verified by the noiseless fixed-offset host test).
             */
            float e = ((float)t->prev_dec * (float)yi -
                       (float)a * (float)t->prev_sym.re) / 32768.0f;
            t->mu = e;   /* last timing error, for inspection */
            ctrl = t->loop_gain * e;
        }

        t->prev_sym  = y;
        t->prev_dec  = (q15_t)a;
        t->have_prev = 1u;

        /* Schedule the next instant one symbol ahead, nudged by the loop. */
        t->next_t += (float)t->sps + ctrl;

        if (out != NULL) {
            *out = y;
        }
        emitted = 1;
    }

    t->last      = sample;
    t->have_last = 1u;
    t->pos       = n + 1.0f;
    return emitted;
}
