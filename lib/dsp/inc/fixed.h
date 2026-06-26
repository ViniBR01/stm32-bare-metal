#ifndef LIB_DSP_FIXED_H
#define LIB_DSP_FIXED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Shared fixed-point conventions for the software-modem sample path
 * (Plan 002 sub-track B0). See docs/wiki/plans/002-dsp-baseband/software-modem.md.
 *
 * Format: q15 == int16_t interpreted as Q1.15 — a signed fraction in the
 * range [-1.0, +1.0). 0x7FFF (Q15_ONE) is the largest representable value,
 * ~+0.99997, and stands in for +1.0; 0x8000 (Q15_MIN) is exactly -1.0.
 *
 * Products of two q15 values are q30 and live in an int32_t accumulator; a
 * rounding right shift of 15 brings them back to q15. All q15 outputs saturate
 * rather than wrap, so an out-of-range intermediate clamps to the endpoints
 * instead of silently changing sign.
 *
 * This header is pure integer arithmetic (plus optional float<->q15 helpers
 * that the host tests and golden-vector generators use); it pulls in no libm
 * and no peripheral, so it compiles unchanged on host and target.
 */

typedef int16_t q15_t;
typedef int32_t q31_t;

#define Q15_ONE   ((q15_t)0x7FFF)   /* +0.99997, represents +1.0          */
#define Q15_MIN   ((q15_t)0x8000)   /* -1.0                               */
#define Q15_MAX   ((q15_t)0x7FFF)   /* alias of Q15_ONE for clarity       */
#define Q15_SHIFT 15

/* Saturate a 32-bit intermediate down to the q15 range (no wraparound). */
static inline q15_t q15_sat(q31_t x)
{
    if (x > (q31_t)Q15_MAX) {
        return Q15_MAX;
    }
    if (x < (q31_t)Q15_MIN) {
        return Q15_MIN;
    }
    return (q15_t)x;
}

/* Saturating q15 addition. */
static inline q15_t q15_add(q15_t a, q15_t b)
{
    return q15_sat((q31_t)a + (q31_t)b);
}

/*
 * q15 * q15 -> q15, with round-to-nearest on the discarded low bits.
 * The (1 << 14) bias rounds the q30 product before the >>15.
 */
static inline q15_t q15_mul(q15_t a, q15_t b)
{
    q31_t prod = (q31_t)a * (q31_t)b;
    return q15_sat((prod + (1 << (Q15_SHIFT - 1))) >> Q15_SHIFT);
}

/*
 * Float<->q15 conversions. Used by host unit tests and the Python-derived
 * golden-vector comparisons; harmless on target (hard-FPU) but the sample
 * path itself stays integer.
 */
static inline q15_t q15_from_float(float x)
{
    float scaled = x * 32768.0f;
    /* Round half away from zero, then saturate. */
    q31_t r = (q31_t)(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
    return q15_sat(r);
}

static inline float q15_to_float(q15_t x)
{
    return (float)x / 32768.0f;
}

#ifdef __cplusplus
}
#endif

#endif /* LIB_DSP_FIXED_H */
