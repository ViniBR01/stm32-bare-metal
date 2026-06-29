#include "nco.h"
#include "sincos.h"

void nco_init(nco_t *o, uint32_t phase0, uint32_t incr)
{
    if (o == NULL) {
        return;
    }
    o->phase = phase0;
    o->incr  = incr;
}

void nco_set_incr(nco_t *o, uint32_t incr)
{
    if (o != NULL) {
        o->incr = incr;
    }
}

cq15_t nco_step(nco_t *o)
{
    q15_t s, c;
    dsp_sincos_q15(o->phase, &s, &c);
    o->phase += o->incr;          /* uint32 wrap == phase wrap at 2*pi */
    return cq15_make(c, s);       /* e^{j*phase} = cos + j sin         */
}
