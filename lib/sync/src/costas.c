#include "costas.h"
#include "sincos.h"
#include "fixed.h"
#include <stddef.h>

void costas_init(costas_t *c, float alpha, float beta)
{
    if (c == NULL) {
        return;
    }
    nco_init(&c->nco, 0u, 0u);
    c->freq  = 0.0f;
    c->alpha = alpha;
    c->beta  = beta;
}

cq15_t costas_step(costas_t *c, cq15_t sym)
{
    if (c == NULL) {
        return sym;
    }

    /*
     * De-rotate by the current NCO phase: y = sym * conj(e^{j phase}). nco_step
     * returns the phasor and advances the phase by the increment set last
     * symbol, so the loop's correction takes effect coherently.
     */
    cq15_t phasor = nco_step(&c->nco);
    cq15_t y = cq15_mul(sym, cq15_conj(phasor));

    /*
     * Decision-directed BPSK phase error: e = sign(y_I) * y_Q, normalised by
     * the q15 unit scale so the loop gains are O(1).
     */
    float yi = (float)y.re;
    float yq = (float)y.im;
    float dec = (yi >= 0.0f) ? 1.0f : -1.0f;
    float e = (dec * yq) / 32768.0f;

    /*
     * Second-order PI loop. The integrator (freq) tracks residual CFO in
     * cycles/symbol; the proportional path nudges instantaneous phase. The next
     * NCO increment is (freq + alpha*e) cycles/symbol. Negative increments map
     * cleanly through the uint32 phase wrap (0.99 cycle == -0.01 cycle).
     */
    c->freq += c->beta * e;
    float incr_cycles = c->freq + c->alpha * e;
    nco_set_incr(&c->nco, nco_phase_from_cycles(incr_cycles));

    return y;
}
