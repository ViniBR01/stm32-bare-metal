#ifndef LIB_PRBS_H
#define LIB_PRBS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pseudo-random bit sequence (PRBS) generator and bit-error checker for the
 * software modem (Plan 002 sub-track B0). See
 * docs/wiki/plans/002-dsp-baseband/software-modem.md.
 *
 * Two maximal-length Fibonacci LFSR polynomials are supported (ITU-T O.150
 * tap assignments):
 *
 *   PRBS-9   x^9  + x^5  + 1   period 2^9  - 1 = 511
 *   PRBS-15  x^15 + x^14 + 1   period 2^15 - 1 = 32767
 *
 * The generator emits one bit per step. The checker runs an identical
 * generator in lock-step with the received bit stream and counts mismatches —
 * the standard way to measure BER when transmitter and receiver share the same
 * seed (which they do in the on-board simulator). True self-synchronising
 * recovery from an unknown phase, and frame sync, arrive with the receiver
 * loops in phase B0.5; here both ends start aligned by construction.
 *
 * Pure integer logic, no peripheral access: compiles unchanged on host and
 * target.
 */

typedef enum {
    PRBS9  = 0,
    PRBS15 = 1,
} prbs_poly_t;

typedef struct {
    uint16_t state;   /* current LFSR state (never zero once seeded)        */
    uint8_t  tap_a;   /* high feedback tap bit index                        */
    uint8_t  tap_b;   /* low feedback tap bit index                         */
    uint16_t mask;    /* width mask: (1 << width) - 1                       */
} prbs_t;

/*
 * Initialise a generator. seed is forced nonzero (a zero seed would lock the
 * LFSR at all-zeros); if seed masks to zero it is replaced by 1. Returns the
 * sequence period (2^width - 1) for the chosen polynomial.
 */
uint32_t prbs_init(prbs_t *p, prbs_poly_t poly, uint16_t seed);

/* Advance the LFSR by one step and return the freshly shifted-in bit (0/1). */
uint8_t prbs_next_bit(prbs_t *p);

/* Fill buf[0..n-1] with the next n bits (each 0 or 1). */
void prbs_next_bits(prbs_t *p, uint8_t *buf, size_t n);

/*
 * Bit-error checker: an internal reference generator plus running counters.
 * Seed it identically to the transmit-side generator, then feed each received
 * bit; every bit that disagrees with the reference increments the error count.
 */
typedef struct {
    prbs_t   ref;
    uint64_t total;
    uint64_t errors;
} prbs_check_t;

/* Initialise a checker with the same polynomial/seed as the transmitter. */
void prbs_check_init(prbs_check_t *c, prbs_poly_t poly, uint16_t seed);

/*
 * Compare one received bit against the reference and advance. Returns 1 if the
 * bit matched, 0 if it was an error. Updates total/errors.
 */
uint8_t prbs_check_bit(prbs_check_t *c, uint8_t rx_bit);

#ifdef __cplusplus
}
#endif

#endif /* LIB_PRBS_H */
