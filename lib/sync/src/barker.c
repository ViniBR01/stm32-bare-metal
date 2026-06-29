#include "barker.h"

/* Barker-13: the canonical length-13 sequence (sidelobes <= 1). */
const int8_t BARKER13[BARKER13_LEN] = {
    +1, +1, +1, +1, +1, -1, -1, +1, +1, -1, +1, -1, +1
};

void barker_init(barker_t *b, int64_t threshold)
{
    if (b == NULL) {
        return;
    }
    for (int i = 0; i < BARKER13_LEN; i++) {
        b->window[i] = cq15_make(0, 0);
    }
    b->head      = 0u;
    b->count     = 0u;
    b->threshold = threshold;
}

int barker_push(barker_t *b, cq15_t sym, int32_t *corr_re, int64_t *mag2)
{
    if (b == NULL) {
        return 0;
    }

    /* Insert the newest symbol into the ring. */
    b->window[b->head] = sym;
    b->head = (uint8_t)((b->head + 1u) % BARKER13_LEN);
    if (b->count < BARKER13_LEN) {
        b->count++;
        if (b->count < BARKER13_LEN) {
            return 0;   /* not enough symbols yet */
        }
    }

    /*
     * Correlate the window against BARKER13. The oldest symbol (at head, the
     * next write slot) aligns with BARKER13[0]. Accumulate the complex sum of
     * sym * sign, then compare its magnitude-squared to the threshold.
     */
    int64_t acc_re = 0;
    int64_t acc_im = 0;
    uint8_t idx = b->head;   /* oldest sample */
    for (int k = 0; k < BARKER13_LEN; k++) {
        int s = BARKER13[k];
        acc_re += (int64_t)b->window[idx].re * s;
        acc_im += (int64_t)b->window[idx].im * s;
        idx = (uint8_t)((idx + 1u) % BARKER13_LEN);
    }

    int64_t m2 = acc_re * acc_re + acc_im * acc_im;
    if (mag2 != NULL) {
        *mag2 = m2;
    }
    if (corr_re != NULL) {
        *corr_re = (int32_t)acc_re;
    }

    return (m2 >= b->threshold) ? 1 : 0;
}
