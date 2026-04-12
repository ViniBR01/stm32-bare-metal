/**
 * @file test_harness.c
 * @brief Unity test harness for hardware-in-the-loop testing
 *
 * This file contains Unity-based tests that run on the STM32 target hardware.
 * Only compiled when HIL_TEST_MODE is defined (via HIL_TEST=1 build flag).
 *
 * Test coverage strategy:
 *
 *   Tier 1 — Smoke tests: all 5 SPI interfaces at max speed (prescaler 2,
 *            256 bytes), both polled and DMA. Verifies every SPI instance
 *            is functional with loopback cables.
 *
 *   Tier 2 — Deep sweep on one SPI per bus:
 *            - SPI2 (APB1, 50 MHz bus → 25 MHz SPI max)
 *            - SPI1 (APB2, 100 MHz bus → 50 MHz SPI max)
 *            Covers multiple prescaler values and buffer sizes to
 *            characterize throughput across the operating range.
 *
 *   Tier 3 — Non-SPI hardware tests (FPU, etc.)
 */

#ifdef HIL_TEST_MODE

#include "unity.h"
#include "spi_perf.h"
#include "test_output.h"
#include "printf.h"
#include "printf_dma.h"

/* ====================================================================
 * Parameterized SPI test infrastructure
 * ==================================================================== */

typedef struct {
    spi_instance_t instance;
    uint16_t       prescaler;
    uint16_t       buffer_size;
    uint8_t        use_dma;
} spi_test_params_t;

static spi_test_params_t current_spi_params;

static void run_spi_test(void) {
    int result = spi_perf_run(
        current_spi_params.instance,
        current_spi_params.prescaler,
        current_spi_params.buffer_size,
        current_spi_params.use_dma);
    TEST_ASSERT_EQUAL_MESSAGE(0, result, "spi_perf_run failed");
}

/**
 * @brief Helper macro to define and run a parameterized SPI test.
 *
 * Unity's RUN_TEST requires a void(void) function pointer and captures
 * __LINE__ for reporting.  We store parameters in a static struct and
 * call a common runner.  The macro expands to a single RUN_TEST call
 * so Unity reports the correct line number.
 */
#define RUN_SPI_TEST(inst, psc, bufsz, dma) do {           \
    current_spi_params.instance    = (inst);                \
    current_spi_params.prescaler   = (psc);                 \
    current_spi_params.buffer_size = (bufsz);               \
    current_spi_params.use_dma     = (dma);                 \
    RUN_TEST(run_spi_test);                                 \
} while (0)

/* ====================================================================
 * Unity setup / teardown
 * ==================================================================== */

void setUp(void) {
    printf_dma_flush();
}

void tearDown(void) {
    printf_dma_flush();
}

/* ====================================================================
 * Non-SPI hardware tests
 * ==================================================================== */

void test_fpu_multiplication(void) {
    volatile float a = 3.14f;
    volatile float b = 2.72f;
    volatile float result = a * b;
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 8.54f, result,
                                     "FPU multiplication incorrect");
}

void test_fpu_division(void) {
    volatile float a = 10.0f;
    volatile float b = 4.0f;
    volatile float result = a / b;
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.001f, 2.5f, result,
                                     "FPU division incorrect");
}

/* ====================================================================
 * Main test runner
 * ==================================================================== */

int run_unity_tests(void) {
    UNITY_BEGIN();

    /* ----------------------------------------------------------
     * Tier 1: Smoke test — all 5 SPIs at max speed
     *   prescaler=2, 256 bytes, polled + DMA
     * ---------------------------------------------------------- */
    printf("\n--- Tier 1: All-SPI smoke (psc=2, 256B) ---\n");
    printf_dma_flush();

    RUN_SPI_TEST(SPI_INSTANCE_1, 2, 256, 0);   /* SPI1 polled */
    RUN_SPI_TEST(SPI_INSTANCE_1, 2, 256, 1);   /* SPI1 DMA    */
    RUN_SPI_TEST(SPI_INSTANCE_2, 2, 256, 0);   /* SPI2 polled */
    RUN_SPI_TEST(SPI_INSTANCE_2, 2, 256, 1);   /* SPI2 DMA    */
    RUN_SPI_TEST(SPI_INSTANCE_3, 2, 256, 0);   /* SPI3 polled */
    RUN_SPI_TEST(SPI_INSTANCE_3, 2, 256, 1);   /* SPI3 DMA    */
    RUN_SPI_TEST(SPI_INSTANCE_4, 2, 256, 0);   /* SPI4 polled */
    RUN_SPI_TEST(SPI_INSTANCE_4, 2, 256, 1);   /* SPI4 DMA    */
    RUN_SPI_TEST(SPI_INSTANCE_5, 2, 256, 0);   /* SPI5 polled */
    RUN_SPI_TEST(SPI_INSTANCE_5, 2, 256, 1);   /* SPI5 DMA    */

    /* ----------------------------------------------------------
     * Tier 2a: Deep sweep — SPI2 (APB1, 50 MHz bus)
     *   Prescaler sweep: 2, 4, 8, 16, 32, 64, 128, 256
     *   Buffer sizes: 1, 4, 16, 64, 256
     *   Both polled and DMA
     *
     *   Full matrix is 8×5×2 = 80 tests. We select a representative
     *   subset: all prescalers at 256B, all sizes at prescaler=2,
     *   plus DMA mirrors. This gives thorough coverage with ~30 tests.
     * ---------------------------------------------------------- */
    printf("\n--- Tier 2a: SPI2 (APB1 50MHz) deep sweep ---\n");
    printf_dma_flush();

    /* Prescaler sweep at 256 bytes — polled */
    RUN_SPI_TEST(SPI_INSTANCE_2, 2,   256, 0);
    RUN_SPI_TEST(SPI_INSTANCE_2, 4,   256, 0);
    RUN_SPI_TEST(SPI_INSTANCE_2, 8,   256, 0);
    RUN_SPI_TEST(SPI_INSTANCE_2, 16,  256, 0);
    RUN_SPI_TEST(SPI_INSTANCE_2, 32,  256, 0);
    RUN_SPI_TEST(SPI_INSTANCE_2, 64,  256, 0);
    RUN_SPI_TEST(SPI_INSTANCE_2, 128, 256, 0);
    RUN_SPI_TEST(SPI_INSTANCE_2, 256, 256, 0);

    /* Prescaler sweep at 256 bytes — DMA */
    RUN_SPI_TEST(SPI_INSTANCE_2, 2,   256, 1);
    RUN_SPI_TEST(SPI_INSTANCE_2, 4,   256, 1);
    RUN_SPI_TEST(SPI_INSTANCE_2, 8,   256, 1);
    RUN_SPI_TEST(SPI_INSTANCE_2, 16,  256, 1);
    RUN_SPI_TEST(SPI_INSTANCE_2, 32,  256, 1);
    RUN_SPI_TEST(SPI_INSTANCE_2, 64,  256, 1);
    RUN_SPI_TEST(SPI_INSTANCE_2, 128, 256, 1);
    RUN_SPI_TEST(SPI_INSTANCE_2, 256, 256, 1);

    /* Buffer size sweep at prescaler=2 — polled */
    RUN_SPI_TEST(SPI_INSTANCE_2, 2, 1,   0);
    RUN_SPI_TEST(SPI_INSTANCE_2, 2, 4,   0);
    RUN_SPI_TEST(SPI_INSTANCE_2, 2, 16,  0);
    RUN_SPI_TEST(SPI_INSTANCE_2, 2, 64,  0);

    /* Buffer size sweep at prescaler=2 — DMA */
    RUN_SPI_TEST(SPI_INSTANCE_2, 2, 1,   1);
    RUN_SPI_TEST(SPI_INSTANCE_2, 2, 4,   1);
    RUN_SPI_TEST(SPI_INSTANCE_2, 2, 16,  1);
    RUN_SPI_TEST(SPI_INSTANCE_2, 2, 64,  1);

    /* ----------------------------------------------------------
     * Tier 2b: Deep sweep — SPI1 (APB2, 100 MHz bus)
     *   Same test matrix structure as SPI2.
     * ---------------------------------------------------------- */
    printf("\n--- Tier 2b: SPI1 (APB2 100MHz) deep sweep ---\n");
    printf_dma_flush();

    /* Prescaler sweep at 256 bytes — polled */
    RUN_SPI_TEST(SPI_INSTANCE_1, 2,   256, 0);
    RUN_SPI_TEST(SPI_INSTANCE_1, 4,   256, 0);
    RUN_SPI_TEST(SPI_INSTANCE_1, 8,   256, 0);
    RUN_SPI_TEST(SPI_INSTANCE_1, 16,  256, 0);
    RUN_SPI_TEST(SPI_INSTANCE_1, 32,  256, 0);
    RUN_SPI_TEST(SPI_INSTANCE_1, 64,  256, 0);
    RUN_SPI_TEST(SPI_INSTANCE_1, 128, 256, 0);
    RUN_SPI_TEST(SPI_INSTANCE_1, 256, 256, 0);

    /* Prescaler sweep at 256 bytes — DMA */
    RUN_SPI_TEST(SPI_INSTANCE_1, 2,   256, 1);
    RUN_SPI_TEST(SPI_INSTANCE_1, 4,   256, 1);
    RUN_SPI_TEST(SPI_INSTANCE_1, 8,   256, 1);
    RUN_SPI_TEST(SPI_INSTANCE_1, 16,  256, 1);
    RUN_SPI_TEST(SPI_INSTANCE_1, 32,  256, 1);
    RUN_SPI_TEST(SPI_INSTANCE_1, 64,  256, 1);
    RUN_SPI_TEST(SPI_INSTANCE_1, 128, 256, 1);
    RUN_SPI_TEST(SPI_INSTANCE_1, 256, 256, 1);

    /* Buffer size sweep at prescaler=2 — polled */
    RUN_SPI_TEST(SPI_INSTANCE_1, 2, 1,   0);
    RUN_SPI_TEST(SPI_INSTANCE_1, 2, 4,   0);
    RUN_SPI_TEST(SPI_INSTANCE_1, 2, 16,  0);
    RUN_SPI_TEST(SPI_INSTANCE_1, 2, 64,  0);

    /* Buffer size sweep at prescaler=2 — DMA */
    RUN_SPI_TEST(SPI_INSTANCE_1, 2, 1,   1);
    RUN_SPI_TEST(SPI_INSTANCE_1, 2, 4,   1);
    RUN_SPI_TEST(SPI_INSTANCE_1, 2, 16,  1);
    RUN_SPI_TEST(SPI_INSTANCE_1, 2, 64,  1);

    /* ----------------------------------------------------------
     * Tier 3: Non-SPI hardware tests
     * ---------------------------------------------------------- */
    printf("\n--- Tier 3: FPU tests ---\n");
    printf_dma_flush();

    RUN_TEST(test_fpu_multiplication);
    RUN_TEST(test_fpu_division);

    printf_dma_flush();
    return UNITY_END();
}

#endif /* HIL_TEST_MODE */
