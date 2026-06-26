#include "awgn.h"
#include <math.h>

/*
 * xorshift128 (Marsaglia). Small, fast, and fully deterministic — adequate for
 * reproducible noise in a teaching/measurement modem (not for cryptography).
 * The 4-word state is seeded from a single 32-bit value via a splitmix32-style
 * expansion so callers only have to supply one seed.
 */

void awgn_prng_seed(awgn_prng_t *rng, uint32_t seed)
{
    uint32_t z = seed;
    for (int i = 0; i < 4; i++) {
        z += 0x9E3779B9u;            /* golden-ratio increment (splitmix) */
        uint32_t x = z;
        x = (x ^ (x >> 16)) * 0x85EBCA6Bu;
        x = (x ^ (x >> 13)) * 0xC2B2AE35u;
        x =  x ^ (x >> 16);
        rng->s[i] = x;
    }
    /* xorshift128 must not be all-zero state. */
    if ((rng->s[0] | rng->s[1] | rng->s[2] | rng->s[3]) == 0u) {
        rng->s[0] = 1u;
    }
    /* Drop any cached Gaussian partner so a reseed gives a fresh stream. */
    rng->gauss_spare = 0.0f;
    rng->have_spare  = 0u;
}

uint32_t awgn_prng_u32(awgn_prng_t *rng)
{
    uint32_t t = rng->s[3];
    uint32_t s = rng->s[0];
    rng->s[3] = rng->s[2];
    rng->s[2] = rng->s[1];
    rng->s[1] = s;
    t ^= t << 11;
    t ^= t >> 8;
    rng->s[0] = t ^ s ^ (s >> 19);
    return rng->s[0];
}

/* Uniform float in (0, 1]: avoids exactly 0 so log() in Box-Muller is safe. */
static float prng_unit(awgn_prng_t *rng)
{
    /* Top 24 bits -> [0, 2^24); map to (0, 1]. */
    uint32_t u = awgn_prng_u32(rng) >> 8;
    return ((float)u + 1.0f) / 16777216.0f;
}

float awgn_prng_gauss(awgn_prng_t *rng)
{
    /*
     * Box-Muller generates two independent N(0,1) samples per pair of uniforms.
     * We return one per call and cache its partner (in the PRNG state, so the
     * cache is per-instance and cleared on reseed) for the next call.
     */
    if (rng->have_spare) {
        rng->have_spare = 0u;
        return rng->gauss_spare;
    }

    float u1 = prng_unit(rng);
    float u2 = prng_unit(rng);
    float mag = sqrtf(-2.0f * logf(u1));
    float ang = 6.283185307179586f * u2;   /* 2*pi */

    rng->gauss_spare = mag * sinf(ang);
    rng->have_spare  = 1u;
    return mag * cosf(ang);
}

float channel_awgn_sigma(float ebn0_db)
{
    /* sigma = sqrt( 1 / (2 * 10^(dB/10)) ) for unit-energy BPSK. */
    float ebn0_lin = powf(10.0f, ebn0_db / 10.0f);
    return sqrtf(1.0f / (2.0f * ebn0_lin));
}

double channel_awgn_theory_ber(float ebn0_db)
{
    /* BER = 0.5 * erfc( sqrt(Eb/N0_linear) ). */
    double ebn0_lin = pow(10.0, (double)ebn0_db / 10.0);
    return 0.5 * erfc(sqrt(ebn0_lin));
}

void channel_awgn_apply(q15_t *samples, size_t n, float ebn0_db, awgn_prng_t *rng)
{
    if (samples == NULL || rng == NULL) {
        return;
    }

    float sigma = channel_awgn_sigma(ebn0_db);
    /* Noise scaled onto the q15 unit-symbol scale (+/-1.0 == +/-32768). */
    float scale = sigma * 32768.0f;

    for (size_t i = 0; i < n; i++) {
        float noise = awgn_prng_gauss(rng) * scale;
        q31_t noisy = (q31_t)samples[i] + (q31_t)lrintf(noise);
        samples[i] = q15_sat(noisy);
    }
}
