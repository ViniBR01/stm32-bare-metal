#ifndef LIB_DSP_COMPLEXQ15_H
#define LIB_DSP_COMPLEXQ15_H

#include <stdint.h>
#include "fixed.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Complex q15 baseband sample (Plan 002 sub-track B0.5). See
 * docs/wiki/plans/002-dsp-baseband/software-modem.md.
 *
 * Through B0.4 the modem sample path was real-valued: BPSK transmits a real
 * +/-1.0 symbol and the channel added real noise. B0.5 introduces carrier
 * frequency offset, phase offset, and a Costas phase-recovery loop, all of
 * which are rotations in the complex plane, so the sample path becomes complex
 * from the impairment channel onward. cq15_t carries the in-phase (re) and
 * quadrature (im) components, each a Q1.15 fraction exactly like q15_t.
 *
 * The transmit path and the RRC shaper stay real (Q == 0 at the transmitter);
 * complex only appears where the physics requires it. All arithmetic reuses the
 * q15 helpers in fixed.h, so rounding (+1<<14 before >>15) and saturation match
 * the rest of the sample path bit-for-bit.
 *
 * Header-only (static inline), pure integer: compiles unchanged on host and
 * target, mirrors fixed.h.
 */

typedef struct {
    q15_t re;
    q15_t im;
} cq15_t;

/* Construct a complex sample from its components. */
static inline cq15_t cq15_make(q15_t re, q15_t im)
{
    cq15_t c = { re, im };
    return c;
}

/* Promote a real q15 to complex (quadrature zero). */
static inline cq15_t cq15_from_real(q15_t x)
{
    cq15_t c = { x, 0 };
    return c;
}

/* Extract the in-phase (real) component. */
static inline q15_t cq15_real(cq15_t a)
{
    return a.re;
}

/* Componentwise saturating complex addition. */
static inline cq15_t cq15_add(cq15_t a, cq15_t b)
{
    return cq15_make(q15_add(a.re, b.re), q15_add(a.im, b.im));
}

/*
 * Complex multiply: (a.re + j a.im)(b.re + j b.im)
 *   re = a.re*b.re - a.im*b.im
 *   im = a.re*b.im + a.im*b.re
 *
 * Each component sums two q30 products in a single 64-bit accumulator and
 * rounds (+1<<14) then shifts (>>15) and saturates exactly once — never add two
 * pre-rounded q15 values, which would double-round. A 64-bit accumulator is
 * required, not q31: the sum of two q30 products reaches 2*(2^30) = 2^31, which
 * overflows int32 (e.g. the (-1-j)^2 corner). The result still saturates into
 * q15, sharing the 1-LSB constellation asymmetry documented in
 * lib/modem/inc/bpsk.h; benign for the rotations B0.5 uses, where |a| and |b|
 * are ~1 and the products stay in range.
 */
static inline cq15_t cq15_mul(cq15_t a, cq15_t b)
{
    int64_t re = (int64_t)a.re * b.re - (int64_t)a.im * b.im;
    int64_t im = (int64_t)a.re * b.im + (int64_t)a.im * b.re;
    q15_t re_q = q15_sat((q31_t)((re + (1 << (Q15_SHIFT - 1))) >> Q15_SHIFT));
    q15_t im_q = q15_sat((q31_t)((im + (1 << (Q15_SHIFT - 1))) >> Q15_SHIFT));
    return cq15_make(re_q, im_q);
}

/* Complex conjugate (negate quadrature). */
static inline cq15_t cq15_conj(cq15_t a)
{
    /* -Q15_MIN saturates to Q15_MAX, preserving the [-1,+1) range. */
    return cq15_make(a.re, q15_sat(-(q31_t)a.im));
}

/*
 * Scale a complex sample by a real q15 gain (the AGC operation). Both
 * components round and saturate independently, like q15_mul.
 */
static inline cq15_t cq15_scale_real(cq15_t a, q15_t g)
{
    return cq15_make(q15_mul(a.re, g), q15_mul(a.im, g));
}

/*
 * Real part of a * conj(b) = a.re*b.re + a.im*b.im, returned as a q31 (q30
 * scale) accumulator without rounding back to q15. Useful for the timing- and
 * phase-error detectors, which want the full-precision dot product rather than
 * a saturated q15.
 */
static inline q31_t cq15_dot_re(cq15_t a, cq15_t b)
{
    return (q31_t)a.re * (q31_t)b.re + (q31_t)a.im * (q31_t)b.im;
}

#ifdef __cplusplus
}
#endif

#endif /* LIB_DSP_COMPLEXQ15_H */
