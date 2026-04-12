/**
 * @file test_harness.c
 * @brief Unity test harness for hardware-in-the-loop testing
 *
 * This file contains Unity-based tests that run on the STM32 target hardware.
 * Only compiled when HIL_TEST_MODE is defined (via HIL_TEST=1 build flag).
 *
 * Tests verify hardware behavior including:
 * - SPI performance and correctness (polled and DMA modes)
 * - FPU operation (hardware floating-point math)
 * - Other driver functionality as tests are added
 */

#ifdef HIL_TEST_MODE

#include "unity.h"
#include "spi_perf.h"
#include "test_output.h"
#include "printf.h"

/* Unity setup/teardown functions (required by Unity framework) */
void setUp(void) {
    /* Called before each test - currently no per-test setup needed */
}

void tearDown(void) {
    /* Called after each test - currently no per-test cleanup needed */
}

/**
 * @brief Test SPI performance in polled mode with small buffer
 *
 * Verifies that SPI2 can successfully transmit/receive in polled mode.
 * Uses a small 3-byte buffer for fast execution.
 */
void test_spi_perf_polled_3bytes(void) {
    int result = spi_perf_run(SPI_INSTANCE_2, 4, 3, 0);
    TEST_ASSERT_EQUAL_MESSAGE(0, result, "SPI polled mode failed");
}

/**
 * @brief Test SPI performance in DMA mode with large buffer
 *
 * Verifies that SPI2 can successfully transmit/receive using DMA.
 * Uses a 256-byte buffer to stress DMA transfer logic.
 */
void test_spi_perf_dma_256bytes(void) {
    int result = spi_perf_run(SPI_INSTANCE_2, 4, 256, 1);
    TEST_ASSERT_EQUAL_MESSAGE(0, result, "SPI DMA mode failed");
}

/**
 * @brief Test hardware FPU floating-point operations
 *
 * Verifies that the hardware FPU is enabled and working correctly.
 * Tests basic floating-point arithmetic (multiplication).
 */
void test_fpu_operations(void) {
    volatile float a = 3.14f;
    volatile float b = 2.72f;
    volatile float result = a * b;
    
    /* Expected: 3.14 * 2.72 = 8.5408 */
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 8.54f, result,
                                     "FPU multiplication failed");
}

/**
 * @brief Test FPU division operation
 *
 * Verifies FPU can perform division correctly.
 */
void test_fpu_division(void) {
    volatile float a = 10.0f;
    volatile float b = 4.0f;
    volatile float result = a / b;
    
    /* Expected: 10.0 / 4.0 = 2.5 */
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.001f, 2.5f, result,
                                     "FPU division failed");
}

/**
 * @brief Run all Unity tests
 *
 * Executes all registered test cases and returns the Unity exit code.
 * Called by the run_all_tests CLI command.
 *
 * @return Unity exit code (0 = all tests passed)
 */
int run_unity_tests(void) {
    UNITY_BEGIN();
    
    printf("\n=== Running Unity Hardware Tests ===\n");
    
    RUN_TEST(test_spi_perf_polled_3bytes);
    RUN_TEST(test_spi_perf_dma_256bytes);
    RUN_TEST(test_fpu_operations);
    RUN_TEST(test_fpu_division);
    
    return UNITY_END();
}

#endif /* HIL_TEST_MODE */
