#include "sincos.h"
#include "sincos_lut.h"
#include <math.h>

/*
 * Full-circle phase is uint32. The table has SINCOS_LUT_N entries over 2*pi, so
 * the top log2(N) bits of the phase select the entry and the next bits give the
 * interpolation fraction toward the following entry (wrapping at the end).
 */
#define LUT_INDEX_BITS 10                      /* log2(SINCOS_LUT_N), N == 1024 */
#define LUT_FRAC_BITS  (32 - LUT_INDEX_BITS)   /* remaining low bits = fraction */

#if (1 << LUT_INDEX_BITS) != SINCOS_LUT_N
#error "LUT_INDEX_BITS must match SINCOS_LUT_N"
#endif

/* Linear interpolation between table entries idx and idx+1 (q15 result). */
static q15_t lut_interp(uint32_t phase)
{
    uint32_t idx  = phase >> LUT_FRAC_BITS;
    uint32_t frac = phase & ((1u << LUT_FRAC_BITS) - 1u);   /* 0 .. 2^22-1     */

    int32_t a = sincos_lut[idx];
    int32_t b = sincos_lut[(idx + 1u) & (SINCOS_LUT_N - 1u)];

    /* a + (b - a) * frac, with frac normalised by 2^LUT_FRAC_BITS, rounded. */
    int32_t delta = b - a;
    int64_t step  = ((int64_t)delta * (int64_t)frac +
                     (1ll << (LUT_FRAC_BITS - 1))) >> LUT_FRAC_BITS;
    return q15_sat(a + (q31_t)step);
}

void dsp_sincos_q15(uint32_t phase, q15_t *sin_out, q15_t *cos_out)
{
    if (sin_out != NULL) {
        *sin_out = lut_interp(phase);
    }
    if (cos_out != NULL) {
        /* cos(x) = sin(x + pi/2); a quarter circle is 2^32 / 4 = 0x40000000. */
        *cos_out = lut_interp(phase + 0x40000000u);
    }
}

uint32_t nco_phase_from_cycles(float cycles_per_sample)
{
    /*
     * Map cycles/sample to a uint32 phase increment: 1.0 cycle == 2^32. Reduce
     * to [0,1) first so the float multiply stays exact for the fractional part;
     * the uint32 cast then wraps anything outside one period.
     */
    float frac = cycles_per_sample - floorf(cycles_per_sample);
    return (uint32_t)(frac * 4294967296.0f);   /* 2^32 */
}

uint32_t nco_phase_from_rad(float radians)
{
    const float two_pi = 6.283185307179586f;
    float cycles = radians / two_pi;
    return nco_phase_from_cycles(cycles);
}
