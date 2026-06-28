#include "rrc.h"
#include <math.h>

/*
 * Closed-form root-raised-cosine impulse response at time t (in symbol
 * periods), roll-off beta. Two points have a 0/0 removable singularity and are
 * handled separately:
 *   t == 0           : 1 - beta + 4*beta/pi
 *   |4*beta*t| == 1  : the L'Hopital limit below
 * Everything is double precision; quantisation to q15 happens in rrc_design().
 */
static double rrc_h(double t, double beta)
{
    const double pi = 3.14159265358979323846;

    if (fabs(t) < 1e-10) {
        return 1.0 - beta + 4.0 * beta / pi;
    }

    /* Singularity at the denominator zero 1 - (4*beta*t)^2 == 0. */
    if (beta > 0.0 && fabs(fabs(4.0 * beta * t) - 1.0) < 1e-9) {
        double a = pi / (4.0 * beta);
        return (beta / sqrt(2.0)) *
               ((1.0 + 2.0 / pi) * sin(a) + (1.0 - 2.0 / pi) * cos(a));
    }

    double num = sin(pi * t * (1.0 - beta)) +
                 4.0 * beta * t * cos(pi * t * (1.0 + beta));
    double den = pi * t * (1.0 - (4.0 * beta * t) * (4.0 * beta * t));
    return num / den;
}

/* Round half away from zero, then saturate to q15. Matches q15_from_float. */
static q15_t q15_round_sat(double x)
{
    double scaled = x * 32768.0;
    double r = (scaled >= 0.0) ? floor(scaled + 0.5) : ceil(scaled - 0.5);
    if (r > 32767.0) {
        return Q15_MAX;
    }
    if (r < -32768.0) {
        return Q15_MIN;
    }
    return (q15_t)r;
}

uint8_t rrc_design(rrc_t *f, float beta, uint8_t sps, uint8_t span)
{
    if (f == NULL) {
        return 0;
    }
    if (beta < 0.0f || beta > 1.0f) {
        return 0;
    }
    if (sps < 2u || sps > RRC_MAX_SPS) {
        return 0;
    }
    if (span < 2u || span > RRC_MAX_SPAN) {
        return 0;
    }

    uint16_t ntaps = (uint16_t)sps * span + 1u;

    /*
     * Compute float taps and their energy, then normalise to unit energy
     * (sum of squares == 1) so the matched-filter output preserves Eb = 1.
     * The taps are symmetric about the centre index ntaps/2.
     */
    double centre = (double)(ntaps - 1u) / 2.0;
    double tap[RRC_MAX_TAPS];
    double energy = 0.0;
    for (uint16_t i = 0; i < ntaps; i++) {
        double t = ((double)i - centre) / (double)sps;
        tap[i] = rrc_h(t, (double)beta);
        energy += tap[i] * tap[i];
    }

    double norm = (energy > 0.0) ? 1.0 / sqrt(energy) : 1.0;
    for (uint16_t i = 0; i < ntaps; i++) {
        f->taps[i] = q15_round_sat(tap[i] * norm);
    }

    f->ntaps = (uint8_t)ntaps;
    f->sps   = sps;
    f->span  = span;
    rrc_reset(f);
    return (uint8_t)ntaps;
}

void rrc_reset(rrc_t *f)
{
    if (f == NULL) {
        return;
    }
    for (uint16_t i = 0; i < f->ntaps; i++) {
        f->z[i] = 0;
    }
    f->pos = 0;
}

q15_t rrc_push(rrc_t *f, q15_t x)
{
    /*
     * Circular delay line: store the newest sample, then accumulate
     * taps[k] * sample[now-k] over the window. A 64-bit accumulator covers the
     * worst-case sum over up to RRC_MAX_TAPS taps (~2^34) without overflow.
     */
    uint16_t nt = f->ntaps;
    if (f->pos == 0u) {
        f->pos = nt;
    }
    f->pos--;
    f->z[f->pos] = x;

    int64_t acc = 0;
    uint16_t idx = f->pos;
    for (uint16_t k = 0; k < nt; k++) {
        acc += (int64_t)f->taps[k] * (int64_t)f->z[idx];
        idx++;
        if (idx == nt) {
            idx = 0;
        }
    }

    /* Round (+1<<14) then arithmetic >>15 back to q15, with saturation. */
    int64_t y = (acc + (1 << (Q15_SHIFT - 1))) >> Q15_SHIFT;
    if (y > (int64_t)Q15_MAX) {
        return Q15_MAX;
    }
    if (y < (int64_t)Q15_MIN) {
        return Q15_MIN;
    }
    return (q15_t)y;
}

void rrc_tx_shape(rrc_t *f, const q15_t *syms, size_t nsyms, q15_t *out)
{
    if (f == NULL || syms == NULL || out == NULL) {
        return;
    }
    uint8_t sps = f->sps;
    size_t o = 0;
    for (size_t k = 0; k < nsyms; k++) {
        /* First phase carries the symbol; the rest are zero-stuffed. */
        out[o++] = rrc_push(f, syms[k]);
        for (uint8_t p = 1; p < sps; p++) {
            out[o++] = rrc_push(f, 0);
        }
    }
}

void rrc_rx_match(rrc_t *f, const q15_t *samples, size_t nsamps, q15_t *out)
{
    if (f == NULL || samples == NULL || out == NULL) {
        return;
    }
    for (size_t i = 0; i < nsamps; i++) {
        out[i] = rrc_push(f, samples[i]);
    }
}
