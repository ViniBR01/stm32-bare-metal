#include "agc.h"
#include "fixed.h"
#include <stddef.h>
#include <math.h>

void agc_init(agc_t *a, float ref, float mu, float gain0)
{
    if (a == NULL) {
        return;
    }
    a->ref  = ref;
    a->mu   = mu;
    a->gain = (gain0 > 0.0f) ? gain0 : 1.0f;
}

cq15_t agc_apply(agc_t *a, cq15_t x)
{
    if (a == NULL) {
        return x;
    }

    /* Convert the float gain to q15 (saturating) and scale the sample. */
    q15_t g_q15 = q15_from_float(a->gain > 1.0f ? 1.0f : a->gain);
    /*
     * gain can exceed 1.0; q15 can't represent that, so apply the integer part
     * by repeated/explicit scaling. In practice the loop holds gain near
     * ref/|input| which is O(1); for gains >1 we scale via a float multiply on
     * each component, then saturate. This keeps the AGC honest for attenuating
     * channels (gain slightly >1) without a fixed-point overflow.
     */
    cq15_t y;
    if (a->gain <= 1.0f) {
        y = cq15_scale_real(x, g_q15);
    } else {
        q31_t yr = (q31_t)lrintf((float)x.re * a->gain);
        q31_t yi = (q31_t)lrintf((float)x.im * a->gain);
        y = cq15_make(q15_sat(yr), q15_sat(yi));
    }

    /* Magnitude of the scaled output on the unit scale (1.0 == 32768). */
    float mag = sqrtf((float)y.re * y.re + (float)y.im * y.im) / 32768.0f;

    /* LMS gain update toward the reference magnitude. */
    a->gain += a->mu * (a->ref - mag);
    if (a->gain < 0.0f) {
        a->gain = 0.0f;
    }

    return y;
}
