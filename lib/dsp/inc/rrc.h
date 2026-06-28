#ifndef LIB_DSP_RRC_H
#define LIB_DSP_RRC_H

#include <stdint.h>
#include <stddef.h>
#include "fixed.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Root-raised-cosine (RRC) pulse shaping + matched filter for the software
 * modem (Plan 002 sub-track B0.4). See
 * docs/wiki/plans/002-dsp-baseband/software-modem.md.
 *
 * Until B0.3 one symbol was one sample: bits mapped straight to +/-1.0 and the
 * channel added noise per symbol. B0.4 moves to real oversampled waveforms.
 * The transmitter upsamples the symbol stream by SPS (samples-per-symbol),
 * inserting SPS-1 zeros between symbols, and convolves it with an RRC FIR. The
 * receiver applies the *same* RRC as a matched filter, then decimates at the
 * symbol instants. Splitting the raised-cosine pulse as RRC-on-TX times
 * RRC-on-RX is what makes the cascade a full raised cosine — Nyquist's
 * zero-ISI pulse, whose only nonzero symbol-spaced sample is the one it
 * belongs to.
 *
 * Two properties anchor the design and the tests:
 *
 *   1. ISI-free at Nyquist instants. The TX*RX cascade (a raised cosine) is
 *      exactly zero at every nonzero multiple of SPS, so a clean (noiseless)
 *      chain recovers every bit: BER == 0.
 *
 *   2. Energy-preserving matched filter. The taps are normalised to unit
 *      energy (sum of squares == 1). With +/-1.0 symbols the matched-filter
 *      output at the symbol instant carries the same Eb = 1 as the symbol-level
 *      model did, so the existing AWGN channel and Eb/N0->sigma mapping apply
 *      unchanged at sample rate and the measured BER still tracks the BPSK
 *      theory curve. The matched filter is information-lossless.
 *
 * Fixed-point notes (see fixed.h for the q15 conventions):
 *   - Taps are q15. Unit-energy normalisation keeps the peak tap well inside
 *     range, and a representative random TX waveform peaks around 0.8 (no
 *     saturation in practice).
 *   - The FIR accumulator is 64-bit: a worst-case dot product over up to ~129
 *     taps can reach ~2^34, which overflows q31. Each output is rounded
 *     (+1<<14 before >>15) and saturated back to q15.
 *
 * Pure C plus libm (sinf/cosf/sqrtf for tap design, same as lib/channel); no
 * peripheral access, so it compiles unchanged on host and target.
 */

/*
 * Upper bound on tap count for static allocation: SPS*span + 1 with the most
 * generous configuration this module accepts (SPS<=8, span<=16 -> 129 taps).
 * The default modem config (beta=0.35, SPS=4, span=8) uses 33.
 */
#define RRC_MAX_SPS   8u
#define RRC_MAX_SPAN  16u
#define RRC_MAX_TAPS  (RRC_MAX_SPS * RRC_MAX_SPAN + 1u)

/*
 * One RRC filter instance: the q15 taps plus its own delay line. A full modem
 * uses two — one for TX shaping, one for the RX matched filter — each with
 * independent state. Designed once with rrc_design(), then streamed.
 */
typedef struct {
    q15_t    taps[RRC_MAX_TAPS];
    q15_t    z[RRC_MAX_TAPS];   /* circular delay line; z[pos] is newest      */
    uint16_t pos;
    uint8_t  ntaps;            /* sps*span + 1                                */
    uint8_t  sps;             /* samples per symbol (upsampling factor)       */
    uint8_t  span;            /* one-sided filter length in symbols           */
} rrc_t;

/*
 * Design a unit-energy q15 RRC filter into f.
 *
 *   beta  roll-off factor in [0,1] (0.35 is the modem default).
 *   sps   samples per symbol, 2..RRC_MAX_SPS.
 *   span  filter span in symbols, 2..RRC_MAX_SPAN. Produces sps*span+1 taps.
 *
 * Taps are computed in double precision from the closed-form RRC impulse
 * response (both removable singularities handled), normalised so the sum of
 * squares is 1, then quantised to q15 with round-half-away-from-zero — the
 * same rule the Python golden-vector generator uses, so they agree to within
 * a couple of LSB. The delay line is zeroed.
 *
 * Returns the tap count (sps*span+1), or 0 if a parameter is out of range
 * (f is left untouched on failure).
 */
uint8_t rrc_design(rrc_t *f, float beta, uint8_t sps, uint8_t span);

/* Zero the delay line (does not touch the taps). Call between independent runs. */
void rrc_reset(rrc_t *f);

/*
 * Push one input sample through the FIR and return one filtered q15 output
 * (rounded, saturated). Maintains the delay line in f.
 */
q15_t rrc_push(rrc_t *f, q15_t x);

/*
 * TX pulse shaping: upsample nsyms symbols by f->sps (zero-stuffing) and filter.
 * Writes nsyms * f->sps samples to out. out must hold that many q15 values.
 */
void rrc_tx_shape(rrc_t *f, const q15_t *syms, size_t nsyms, q15_t *out);

/*
 * RX matched filter: filter nsamps input samples in place-compatible fashion,
 * writing nsamps q15 outputs to out (out may equal samples). The caller then
 * decimates at the symbol instants — see rrc_chain_delay().
 */
void rrc_rx_match(rrc_t *f, const q15_t *samples, size_t nsamps, q15_t *out);

/*
 * Total group delay, in samples, of the TX-shape + RX-match cascade. Symbol k
 * lands at index k*sps + rrc_chain_delay(f) in the matched-filter output
 * stream, so that index is where the RX should sample. Equals ntaps-1 (each
 * linear-phase RRC contributes (ntaps-1)/2).
 */
static inline size_t rrc_chain_delay(const rrc_t *f)
{
    return (size_t)f->ntaps - 1u;
}

#ifdef __cplusplus
}
#endif

#endif /* LIB_DSP_RRC_H */
