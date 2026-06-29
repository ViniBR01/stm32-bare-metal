#include "unity.h"
#include "costas.h"
#include "sincos.h"
#include "nco.h"
#include "bpsk.h"
#include "prbs.h"
#include "complexq15.h"
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

/*
 * Drive symbol-rate BPSK symbols through a fixed phase offset (and optional
 * small CFO) and a Costas loop, and check the loop de-rotates them back onto
 * the real axis so the slicer recovers the bits. Costas leaves a 180-degree
 * ambiguity, so we accept either global polarity (the Barker preamble resolves
 * it in the full chain); the test passes if BER is ~0 OR ~1 (consistently
 * inverted), i.e. min(BER, 1-BER) ~ 0.
 */
static double run_costas(uint32_t phase0, uint32_t cfo_incr,
                         float alpha, float beta, int nbits, int settle)
{
    costas_t c;
    costas_init(&c, alpha, beta);

    prbs_t tx;
    prbs_init(&tx, PRBS9, 1u);

    nco_t chan;            /* injects the phase/CFO impairment */
    nco_init(&chan, phase0, cfo_incr);

    int errors = 0, counted = 0;
    for (int k = 0; k < nbits; k++) {
        uint8_t bit = prbs_next_bit(&tx);
        cq15_t sym = cq15_from_real(bpsk_map(bit));

        /* Apply channel phase/CFO: rotate by the impairment NCO. */
        cq15_t phasor = nco_step(&chan);
        cq15_t rx = cq15_mul(sym, phasor);

        /* Costas de-rotation. */
        cq15_t y = costas_step(&c, rx);

        if (k >= settle) {
            uint8_t dec = bpsk_slice(y.re);
            if (dec != bit) errors++;
            counted++;
        }
    }
    double ber = (counted > 0) ? (double)errors / counted : 1.0;
    return ber < 0.5 ? ber : 1.0 - ber;   /* fold the 180-degree ambiguity */
}

static void test_static_phase_45deg(void)
{
    /* 45 degrees = 2^32/8 = 0x20000000. */
    double ber = run_costas(0x20000000u, 0u, 0.05f, 0.001f, 4000, 1000);
    TEST_ASSERT_TRUE(ber < 1e-3);
}

static void test_static_phase_large_120deg(void)
{
    /* 120 degrees = 2^32/3 ~ 0x55555555. */
    double ber = run_costas(0x55555555u, 0u, 0.05f, 0.001f, 4000, 1500);
    TEST_ASSERT_TRUE(ber < 1e-3);
}

static void test_small_cfo_tracked(void)
{
    /* A small residual CFO (~0.0006 cycle/symbol) plus a phase offset. */
    uint32_t cfo = nco_phase_from_cycles(0.0006f);
    double ber = run_costas(0x10000000u, cfo, 0.05f, 0.002f, 6000, 2500);
    TEST_ASSERT_TRUE(ber < 5e-3);
}

static void test_no_offset_is_clean(void)
{
    double ber = run_costas(0u, 0u, 0.05f, 0.001f, 4000, 200);
    TEST_ASSERT_TRUE(ber < 1e-3);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_offset_is_clean);
    RUN_TEST(test_static_phase_45deg);
    RUN_TEST(test_static_phase_large_120deg);
    RUN_TEST(test_small_cfo_tracked);
    return UNITY_END();
}
