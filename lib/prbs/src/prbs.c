#include "prbs.h"

/*
 * Fibonacci LFSR. The two feedback taps for each supported polynomial are the
 * highest-degree term (width-1) and the second listed term:
 *
 *   PRBS-9   x^9  + x^5  + 1  -> taps at bit indices 8 and 4
 *   PRBS-15  x^15 + x^14 + 1  -> taps at bit indices 14 and 13
 *
 * Each step XORs the tapped bits to form the feedback, shifts the register up
 * by one, and inserts the feedback at bit 0. The inserted bit is what we hand
 * back as the output bit, so generator and checker stay in exact lock-step.
 */

static void lfsr_config(prbs_t *p, prbs_poly_t poly)
{
    if (poly == PRBS15) {
        p->tap_a = 14u;
        p->tap_b = 13u;
        p->mask  = (uint16_t)((1u << 15) - 1u);
    } else { /* PRBS9 (default) */
        p->tap_a = 8u;
        p->tap_b = 4u;
        p->mask  = (uint16_t)((1u << 9) - 1u);
    }
}

uint32_t prbs_init(prbs_t *p, prbs_poly_t poly, uint16_t seed)
{
    lfsr_config(p, poly);

    uint16_t s = (uint16_t)(seed & p->mask);
    if (s == 0u) {
        s = 1u; /* a zero state would lock the LFSR at all-zeros */
    }
    p->state = s;

    /* Maximal-length sequence period: 2^width - 1, which equals the mask. */
    return (uint32_t)p->mask;
}

uint8_t prbs_next_bit(prbs_t *p)
{
    uint8_t fb = (uint8_t)(((p->state >> p->tap_a) ^ (p->state >> p->tap_b)) & 1u);
    p->state = (uint16_t)(((p->state << 1) | fb) & p->mask);
    return fb;
}

void prbs_next_bits(prbs_t *p, uint8_t *buf, size_t n)
{
    if (buf == NULL) {
        return;
    }
    for (size_t i = 0; i < n; i++) {
        buf[i] = prbs_next_bit(p);
    }
}

void prbs_check_init(prbs_check_t *c, prbs_poly_t poly, uint16_t seed)
{
    (void)prbs_init(&c->ref, poly, seed);
    c->total  = 0u;
    c->errors = 0u;
}

uint8_t prbs_check_bit(prbs_check_t *c, uint8_t rx_bit)
{
    uint8_t expected = prbs_next_bit(&c->ref);
    uint8_t matched  = (uint8_t)((rx_bit & 1u) == expected);

    c->total++;
    if (!matched) {
        c->errors++;
    }
    return matched;
}
