#ifndef LIB_DSP_SINCOS_H
#define LIB_DSP_SINCOS_H

#include <stdint.h>
#include <stddef.h>
#include "fixed.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * q15 sine/cosine lookup for the numerically-controlled oscillator (Plan 002
 * sub-track B0.5). See docs/wiki/plans/002-dsp-baseband/software-modem.md.
 *
 * Phase is a uint32_t covering the full circle: 0 == 0 rad, 2^32 == 2*pi. This
 * makes phase wrap free and exact (uint32 overflow is the wrap) and keeps the
 * NCO off the floating-point path on the sample stream. dsp_sincos_q15()
 * returns sin and cos as q15 via a checked-in 1024-entry table with linear
 * interpolation (worst-case error ~0.15 LSB) — deterministic on host and
 * target, unlike runtime libm sinf/cosf which can drift by a ULP across
 * toolchains (the same hazard the AWGN module documents).
 *
 * The float->phase helpers are config-path only (used when a CLI flag or test
 * sets a carrier-frequency or phase offset); the per-sample sample path never
 * touches float here.
 */

/* Write sin(phase) and cos(phase) as q15 for a full-circle uint32 phase. */
void dsp_sincos_q15(uint32_t phase, q15_t *sin_out, q15_t *cos_out);

/*
 * Convert a frequency in cycles-per-sample to a per-sample phase increment.
 * 1.0 cycle/sample wraps the full circle each sample (2^32 per step). Values
 * are taken modulo 1.0 by the uint32 wrap, so any real input is accepted.
 */
uint32_t nco_phase_from_cycles(float cycles_per_sample);

/* Convert an angle in radians to a uint32 phase (2*pi -> 2^32, with wrap). */
uint32_t nco_phase_from_rad(float radians);

#ifdef __cplusplus
}
#endif

#endif /* LIB_DSP_SINCOS_H */
