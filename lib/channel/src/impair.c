#include "impair.h"

void channel_impair_init(channel_impair_state_t *st, const channel_impair_cfg_t *cfg)
{
    if (st == NULL || cfg == NULL) {
        return;
    }
    nco_init(&st->nco, cfg->phase0, cfg->cfo_incr);
    st->prev      = 0;
    st->have_prev = 0u;
}

void channel_impair_apply(channel_impair_state_t *st, const channel_impair_cfg_t *cfg,
                          const q15_t *in_real, cq15_t *out, size_t n)
{
    if (st == NULL || cfg == NULL || in_real == NULL || out == NULL) {
        return;
    }

    q15_t mu = cfg->timing_mu;

    for (size_t i = 0; i < n; i++) {
        q15_t x = in_real[i];

        /*
         * Fractional-sample timing offset by linear interpolation: a delay of
         * mu samples reads (1-mu)*x[n] + mu*x[n-1]. mu == 0 passes x through
         * unchanged. The first sample has no predecessor, so it primes the
         * history (prev defaults to 0, i.e. zero-padding before the stream).
         */
        q15_t delayed;
        if (mu == 0) {
            delayed = x;
        } else {
            q15_t prev = st->have_prev ? st->prev : 0;
            /* (1-mu)*x + mu*prev, computed as x + mu*(prev - x), q15 throughout. */
            q31_t diff = (q31_t)prev - (q31_t)x;
            q31_t blend = (diff * (q31_t)mu + (1 << (Q15_SHIFT - 1))) >> Q15_SHIFT;
            delayed = q15_sat((q31_t)x + blend);
        }
        st->prev      = x;
        st->have_prev = 1u;

        /* Rotate by the NCO phasor (CFO + static phase): out = delayed * e^{jθ}. */
        cq15_t phasor = nco_step(&st->nco);
        out[i] = cq15_mul(cq15_from_real(delayed), phasor);
    }
}
