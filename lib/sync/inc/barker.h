#ifndef LIB_SYNC_BARKER_H
#define LIB_SYNC_BARKER_H

#include <stdint.h>
#include <stddef.h>
#include "complexq15.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Barker-13 preamble frame synchronisation (Plan 002 sub-track B0.5). See
 * docs/wiki/plans/002-dsp-baseband/software-modem.md.
 *
 * A Barker-13 sequence prepended to the payload lets the receiver find the
 * frame boundary and resolve the BPSK 180-degree phase ambiguity that the
 * Costas loop leaves unresolved. The detector slides a 13-symbol window over
 * the incoming symbol stream and correlates against the known sequence; a
 * correlation magnitude above threshold marks the frame.
 *
 * Barker-13 has the lowest autocorrelation sidelobes of any length-13 binary
 * sequence (peak 13, sidelobes <= 1), so the threshold can sit well below the
 * peak and still reject misaligned positions.
 *
 * Correlation uses magnitude-squared (no sqrt) so it is phase-insensitive and
 * locks before the Costas loop fully converges. The sign of the in-phase
 * correlation at the peak gives the BPSK polarity (which of the two phases the
 * preamble was sent in), resolving the 180-degree ambiguity.
 */

#define BARKER13_LEN 13

extern const int8_t BARKER13[BARKER13_LEN];

typedef struct {
    cq15_t   window[BARKER13_LEN];  /* most recent 13 symbols (ring)        */
    uint8_t  head;                  /* ring write index                     */
    uint8_t  count;                 /* symbols seen (saturates at LEN)      */
    int64_t  threshold;             /* |corr|^2 detection threshold         */
} barker_t;

/*
 * Initialise the detector with a magnitude-squared threshold. A practical
 * choice is (frac * 13 * Esym)^2 with frac ~0.6..0.75; Esym is the per-symbol
 * energy on the q15 scale (~32768^2 for unit symbols).
 */
void barker_init(barker_t *b, int64_t threshold);

/*
 * Push one symbol. Returns 1 when the trailing 13-symbol window correlates
 * above threshold (frame found at this position); else 0. On a hit, *corr_re
 * receives the signed in-phase correlation (its sign = recovered BPSK
 * polarity) and *mag2 the magnitude-squared (may be NULL).
 */
int barker_push(barker_t *b, cq15_t sym, int32_t *corr_re, int64_t *mag2);

#ifdef __cplusplus
}
#endif

#endif /* LIB_SYNC_BARKER_H */
