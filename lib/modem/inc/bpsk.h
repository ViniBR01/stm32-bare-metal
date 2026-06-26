#ifndef LIB_MODEM_BPSK_H
#define LIB_MODEM_BPSK_H

#include <stdint.h>
#include <stddef.h>
#include "fixed.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * BPSK symbol mapping/slicing for the software modem (Plan 002 sub-track B0).
 * See docs/wiki/plans/002-dsp-baseband/software-modem.md.
 *
 * Mapping (the conventional BPSK constellation):
 *
 *   bit 0  ->  -1.0   (BPSK_SYM_LO == Q15_MIN)
 *   bit 1  ->  +1.0   (BPSK_SYM_HI == Q15_ONE)
 *
 * Slicing is a sign decision about 0. A received sample of exactly 0 is a tie;
 * we resolve it to bit 1 (treating 0 as the non-negative half-plane). This is
 * an arbitrary but fixed convention so behaviour is deterministic and testable.
 *
 * At this phase one symbol is one sample (no pulse shaping yet); phase B0.4
 * inserts upsampling and the RRC filter between map and channel.
 */

/*
 * Note the constellation is asymmetric by 1 LSB: Q15_MIN is exactly -1.0
 * (-32768) while Q15_ONE is +0.99997 (+32767), per the q15 convention in
 * fixed.h. The resulting energy imbalance is ~1 part in 32768 — far below the
 * noise floor at every SNR of interest — so it is negligible for BER work.
 * Revisit if a future EVM/matched-filter metric (phase B0.4) needs exact
 * symmetry.
 */
#define BPSK_SYM_LO  (Q15_MIN)   /* bit 0 -> -1.0 */
#define BPSK_SYM_HI  (Q15_ONE)   /* bit 1 -> +1.0 */

/* Map a single bit (any nonzero treated as 1) to its BPSK symbol. */
static inline q15_t bpsk_map(uint8_t bit)
{
    return (bit & 1u) ? BPSK_SYM_HI : BPSK_SYM_LO;
}

/* Hard-decision slice: sample >= 0 -> bit 1, sample < 0 -> bit 0. */
static inline uint8_t bpsk_slice(q15_t sample)
{
    return (sample >= 0) ? 1u : 0u;
}

/* Map n bits to n symbols. */
void bpsk_map_block(const uint8_t *bits, q15_t *syms, size_t n);

/* Slice n samples to n bits. */
void bpsk_slice_block(const q15_t *samples, uint8_t *bits, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* LIB_MODEM_BPSK_H */
