#ifndef TEST_OUTPUT_H
#define TEST_OUTPUT_H

/**
 * @file test_output.h
 * @brief Machine-parseable test output macros for HIL testing
 *
 * Provides standardized output format for automated parsing by test runners.
 * Only active when HIL_TEST_MODE is defined (via HIL_TEST=1 build flag).
 *
 * Output format:
 *   START_TESTS
 *   TEST:<name>:<PASS|FAIL>:cycles=<value>:<metric>=<value>
 *   END_TESTS
 *
 * Example:
 *   TEST:spi_perf_polled:PASS:cycles=1234:throughput_kbps=567
 */

#ifdef HIL_TEST_MODE

#include "printf.h"

/**
 * @brief Mark the start of the test sequence
 *
 * Prints START_TESTS marker that the test runner can use to identify
 * the beginning of parseable test output.
 */
#define TEST_OUTPUT_START() printf("START_TESTS\n")

/**
 * @brief Mark the end of the test sequence
 *
 * Prints END_TESTS marker that the test runner can use to identify
 * the end of parseable test output.
 */
#define TEST_OUTPUT_END() printf("END_TESTS\n")

/**
 * @brief Output a test result with performance metrics
 *
 * Emits a machine-parseable test result line.
 *
 * @param name Test name (string literal, no spaces)
 * @param pass Pass/fail boolean (true = PASS, false = FAIL)
 * @param cycles Cycle count measured by DWT
 * @param metric Metric name (string literal, e.g., "throughput_kbps")
 * @param value Metric value (unsigned long)
 *
 * Output format:
 *   TEST:<name>:<PASS|FAIL>:cycles=<cycles>:<metric>=<value>
 */
#define TEST_OUTPUT_RESULT(name, pass, cycles, metric, value) \
    printf("TEST:%s:%s:cycles=%lu:%s=%lu\n", \
           name, (pass) ? "PASS" : "FAIL", (unsigned long)(cycles), metric, (unsigned long)(value))

#else

/* Production builds - all macros are no-ops */
#define TEST_OUTPUT_START()
#define TEST_OUTPUT_END()
#define TEST_OUTPUT_RESULT(name, pass, cycles, metric, value)

#endif /* HIL_TEST_MODE */

#endif /* TEST_OUTPUT_H */
