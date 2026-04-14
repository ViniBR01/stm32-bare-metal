/*
 * timer_calc.h — Pure timer calculation functions (no register access).
 *
 * Exposed for host unit testing. These functions implement the mathematical
 * logic behind timer_pwm_init() and timer_pwm_set_duty(): prescaler selection
 * and duty-cycle CCR mapping. They take plain integers and return plain
 * integers — no peripheral struct access, no side effects.
 */

#ifndef TIMER_CALC_H
#define TIMER_CALC_H

#include <stdint.h>

/**
 * @brief Compute the TIMx->PSC value for a given PWM configuration.
 *
 * PSC = (timer_clk_hz / (pwm_freq_hz * steps)) - 1
 *
 * The timer ticks at pwm_freq_hz * steps per second, completing one PWM
 * period every `steps` ticks. The ARR register is set to steps - 1.
 *
 * @param timer_clk_hz  Timer input clock in Hz (APB1 timer clock)
 * @param pwm_freq_hz   Desired PWM output frequency in Hz
 * @param steps         Number of duty-cycle steps (e.g. 100 → 1% resolution)
 * @return Value to write to TIMx->PSC
 */
uint32_t timer_compute_pwm_psc(uint32_t timer_clk_hz, uint32_t pwm_freq_hz,
                                uint32_t steps);

/**
 * @brief Compute the CCR register value for a duty cycle percentage.
 *
 * CCR = (arr * duty_percent) / 100
 *
 * duty_percent is clamped to [0, 100] before the calculation.
 *
 * @param arr          TIMx->ARR (auto-reload) value in effect
 * @param duty_percent Duty cycle percentage 0-100 (values > 100 are clamped)
 * @return Value to write to the TIMx->CCRx register
 */
uint32_t timer_compute_duty_ccr(uint32_t arr, uint32_t duty_percent);

#endif /* TIMER_CALC_H */
