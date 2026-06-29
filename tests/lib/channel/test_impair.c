#include "unity.h"
#include "impair.h"
#include "awgn.h"
#include "sincos.h"
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

/* --- pass-through when config is identity -------------------------------- */

static void test_identity_passthrough(void)
{
    /* All-zero config: no timing, no CFO, no phase -> out = real input as cq15. */
    channel_impair_cfg_t cfg = { 0u, 0u, 0 };
    channel_impair_state_t st;
    channel_impair_init(&st, &cfg);

    q15_t in[5]  = { 1000, -2000, 32767, -32768, 0 };
    cq15_t out[5];
    channel_impair_apply(&st, &cfg, in, out, 5);

    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_INT16_WITHIN(2, in[i], out[i].re);
        TEST_ASSERT_INT16_WITHIN(2, 0, out[i].im);
    }
}

/* --- static phase offset rotates the constellation ----------------------- */

static void test_static_phase_offset(void)
{
    /* phase0 = pi/2 (0x40000000): a real +1 input becomes +j. */
    channel_impair_cfg_t cfg = { 0u, 0x40000000u, 0 };
    channel_impair_state_t st;
    channel_impair_init(&st, &cfg);

    q15_t in[1] = { 30000 };
    cq15_t out[1];
    channel_impair_apply(&st, &cfg, in, out, 1);

    TEST_ASSERT_INT16_WITHIN(4, 0, out[0].re);       /* cos(pi/2)=0   */
    TEST_ASSERT_INT16_WITHIN(4, 30000, out[0].im);   /* sin(pi/2)=1   */
}

/* --- CFO rotates a constant input as e^{j*incr*n} ------------------------ */

static void test_cfo_rotation(void)
{
    /* A constant real input under a CFO traces a circle: each output is the
     * previous rotated by the per-sample increment. Verify |out| is constant
     * and the phase advances by the expected step. */
    uint32_t incr = 0x04000000u;   /* ~0.0156 cycle/sample */
    channel_impair_cfg_t cfg = { incr, 0u, 0 };
    channel_impair_state_t st;
    channel_impair_init(&st, &cfg);

    const int N = 64;
    q15_t in[64];
    cq15_t out[64];
    for (int i = 0; i < N; i++) {
        in[i] = 20000;
    }
    channel_impair_apply(&st, &cfg, in, out, N);

    for (int i = 0; i < N; i++) {
        double mag = sqrt((double)out[i].re * out[i].re +
                          (double)out[i].im * out[i].im);
        TEST_ASSERT_TRUE(fabs(mag - 20000.0) < 200.0);   /* magnitude preserved */

        /* Expected phase = i * incr, compared via float reference. */
        double theta = (double)((uint32_t)((uint64_t)incr * i)) / 4294967296.0 * 2.0 * M_PI;
        double exp_re = 20000.0 * cos(theta);
        double exp_im = 20000.0 * sin(theta);
        TEST_ASSERT_TRUE(fabs((double)out[i].re - exp_re) < 200.0);
        TEST_ASSERT_TRUE(fabs((double)out[i].im - exp_im) < 200.0);
    }
}

/* --- fractional timing offset via linear interpolation ------------------- */

static void test_timing_offset_linear_interp(void)
{
    /* A delay of mu=0.5 samples on a ramp gives the midpoint average:
     * out[n] = 0.5*x[n] + 0.5*x[n-1]. With x = 0,1000,2000,3000,... the
     * interior outputs are 500,1500,2500,... (and out[0]=0.5*x[0]=0). */
    q15_t mu = 16384;   /* 0.5 in q15 */
    channel_impair_cfg_t cfg = { 0u, 0u, mu };
    channel_impair_state_t st;
    channel_impair_init(&st, &cfg);

    q15_t in[5]  = { 0, 1000, 2000, 3000, 4000 };
    cq15_t out[5];
    channel_impair_apply(&st, &cfg, in, out, 5);

    TEST_ASSERT_INT16_WITHIN(2, 0, out[0].re);      /* 0.5*0 + 0.5*0(prev)   */
    TEST_ASSERT_INT16_WITHIN(2, 500, out[1].re);    /* 0.5*1000 + 0.5*0      */
    TEST_ASSERT_INT16_WITHIN(2, 1500, out[2].re);   /* 0.5*2000 + 0.5*1000   */
    TEST_ASSERT_INT16_WITHIN(2, 2500, out[3].re);
    TEST_ASSERT_INT16_WITHIN(2, 3500, out[4].re);
}

static void test_timing_offset_persists_across_blocks(void)
{
    /* The interpolator history must carry across calls so block boundaries
     * don't inject a discontinuity. Feed the ramp in two halves. */
    q15_t mu = 16384;
    channel_impair_cfg_t cfg = { 0u, 0u, mu };
    channel_impair_state_t st;
    channel_impair_init(&st, &cfg);

    q15_t in1[2] = { 0, 1000 };
    q15_t in2[2] = { 2000, 3000 };
    cq15_t out1[2], out2[2];
    channel_impair_apply(&st, &cfg, in1, out1, 2);
    channel_impair_apply(&st, &cfg, in2, out2, 2);

    /* out2[0] must use the last sample of block 1 (1000) as its predecessor. */
    TEST_ASSERT_INT16_WITHIN(2, 1500, out2[0].re);   /* 0.5*2000 + 0.5*1000 */
    TEST_ASSERT_INT16_WITHIN(2, 2500, out2[1].re);
}

/* --- cq15 AWGN per-axis variance matches sigma --------------------------- */

static void test_cq15_awgn_per_axis_variance(void)
{
    awgn_prng_t rng;
    awgn_prng_seed(&rng, 4242u);

    const int N = 100000;
    static cq15_t buf[100000];
    for (int i = 0; i < N; i++) {
        buf[i] = cq15_make(0, 0);   /* zero signal: pure noise */
    }

    /* 12 dB keeps sigma small (~5820 counts) so the Gaussian tails stay well
     * inside +/-32768 and q15 saturation doesn't bias the measured variance. */
    float ebn0 = 12.0f;
    channel_awgn_apply_cq15(buf, N, ebn0, &rng);

    /* Expected per-axis sigma in q15 counts. */
    float sigma_counts = channel_awgn_sigma(ebn0) * 32768.0f;

    double sre = 0, sqre = 0, sim = 0, sqim = 0;
    for (int i = 0; i < N; i++) {
        sre += buf[i].re;  sqre += (double)buf[i].re * buf[i].re;
        sim += buf[i].im;  sqim += (double)buf[i].im * buf[i].im;
    }
    double var_re = sqre / N - (sre / N) * (sre / N);
    double var_im = sqim / N - (sim / N) * (sim / N);
    double expect_var = (double)sigma_counts * sigma_counts;

    /* Both axes independently track sigma^2 within 5%. */
    TEST_ASSERT_TRUE(fabs(var_re - expect_var) / expect_var < 0.05);
    TEST_ASSERT_TRUE(fabs(var_im - expect_var) / expect_var < 0.05);
}

static void test_null_args_safe(void)
{
    channel_impair_cfg_t cfg = { 0u, 0u, 0 };
    channel_impair_state_t st;
    channel_impair_init(&st, &cfg);
    cq15_t out[1];
    q15_t in[1] = { 0 };
    channel_impair_apply(NULL, &cfg, in, out, 1);
    channel_impair_apply(&st, &cfg, NULL, out, 1);
    channel_awgn_apply_cq15(NULL, 1, 3.0f, NULL);
    TEST_PASS();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_identity_passthrough);
    RUN_TEST(test_static_phase_offset);
    RUN_TEST(test_cfo_rotation);
    RUN_TEST(test_timing_offset_linear_interp);
    RUN_TEST(test_timing_offset_persists_across_blocks);
    RUN_TEST(test_cq15_awgn_per_axis_variance);
    RUN_TEST(test_null_args_safe);
    return UNITY_END();
}
