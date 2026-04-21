#ifndef SYSTICK_H_
#define SYSTICK_H_

#include <stdint.h>

#include "stm32f4xx.h"

/**
 * @brief Configure SysTick for 1 ms tick interrupts and start the counter.
 *
 * Must be called after rcc_init() so that rcc_get_sysclk() returns the
 * correct system clock frequency.  Calling systick_get_ms() before
 * systick_init() always returns 0.
 */
void systick_init(void);

/**
 * @brief Blocking millisecond delay.
 *
 * Spins until at least @p delay milliseconds have elapsed.
 * Returns immediately when delay == 0.
 *
 * @param delay  Number of milliseconds to wait.
 */
void systick_delay_ms(uint32_t delay);

/**
 * @brief Return milliseconds elapsed since systick_init() was called.
 *
 * The counter wraps around after ~49.7 days (UINT32_MAX ms).
 * Use systick_elapsed_since() for wrap-safe elapsed time measurement.
 *
 * @return Millisecond tick count.
 */
uint32_t systick_get_ms(void);

/**
 * @brief Return milliseconds elapsed since @p start_ms.
 *
 * Handles 32-bit counter wraparound correctly via unsigned subtraction.
 *
 * @param start_ms  Snapshot obtained from systick_get_ms().
 * @return          Elapsed milliseconds (always >= 0).
 */
uint32_t systick_elapsed_since(uint32_t start_ms);

#ifdef UNIT_TEST
/**
 * @brief Reset the millisecond tick counter to zero.
 *
 * Available only when compiled with -DUNIT_TEST. Used in host unit test
 * setUp() to ensure a clean state before each test case.
 */
void systick_reset_for_test(void);
#endif

#endif /* SYSTICK_H_ */
