#ifndef TIMER_H_
#define TIMER_H_

#include <stdint.h>

/**
 * @brief Timer instance selection (general-purpose timers on APB1)
 */
typedef enum {
    TIMER_2,    /**< TIM2 (32-bit counter) */
    TIMER_3,    /**< TIM3 (16-bit counter) */
    TIMER_4,    /**< TIM4 (16-bit counter) */
    TIMER_5,    /**< TIM5 (32-bit counter) */
    TIMER_COUNT
} timer_instance_t;

/**
 * @brief Timer output-compare / PWM channel selection
 */
typedef enum {
    TIMER_CH1,
    TIMER_CH2,
    TIMER_CH3,
    TIMER_CH4
} timer_channel_t;

/**
 * @brief Callback type for timer update (overflow) interrupts
 */
typedef void (*timer_callback_t)(void);

/* ---- Basic timer API --------------------------------------------------- */

/**
 * @brief Initialise a general-purpose timer.
 *
 * Enables the peripheral clock, sets PSC and ARR, and resets CNT to 0.
 * The timer is NOT started -- call timer_start() afterwards.
 *
 * @param tim       Timer instance (TIMER_2 .. TIMER_5)
 * @param prescaler Prescaler value written to TIMx->PSC
 * @param period    Auto-reload value written to TIMx->ARR
 */
void timer_init(timer_instance_t tim, uint32_t prescaler, uint32_t period);

/**
 * @brief Start a timer (set CEN bit in CR1).
 * @param tim Timer instance
 */
void timer_start(timer_instance_t tim);

/**
 * @brief Stop a timer (clear CEN bit in CR1).
 * @param tim Timer instance
 */
void timer_stop(timer_instance_t tim);

/**
 * @brief Change the auto-reload (period) value while the timer may be running.
 * @param tim    Timer instance
 * @param period New ARR value
 */
void timer_set_period(timer_instance_t tim, uint32_t period);

/**
 * @brief Register (or clear) the update-interrupt callback for a timer.
 *
 * When @p cb is non-NULL the update interrupt (UIE) is enabled and the
 * NVIC IRQ is unmasked.  Passing NULL disables the update interrupt.
 *
 * @param tim Timer instance
 * @param cb  Callback invoked from ISR context on every update event
 */
void timer_register_callback(timer_instance_t tim, timer_callback_t cb);

/* ---- PWM API ----------------------------------------------------------- */

/**
 * @brief Configure a timer + channel for PWM output.
 *
 * Sets PSC so that the timer ticks at pwm_freq_hz * steps, and ARR = steps-1.
 * Configures the selected channel for PWM mode 1 with preload enabled.
 * The timer is NOT started -- call timer_start() afterwards.
 *
 * NOTE: GPIO alternate-function muxing is the caller's responsibility.
 *
 * @param tim         Timer instance
 * @param ch          Channel (TIMER_CH1 .. TIMER_CH4)
 * @param pwm_freq_hz Desired PWM frequency in Hz
 * @param steps       Number of discrete duty-cycle steps (e.g. 100 for 1 %)
 */
void timer_pwm_init(timer_instance_t tim, timer_channel_t ch,
                    uint32_t pwm_freq_hz, uint32_t steps);

/**
 * @brief Set the duty cycle for a PWM channel.
 *
 * @p duty_percent is clamped to [0, 100] and mapped to the CCR register
 * based on the current ARR value.
 *
 * @param tim          Timer instance
 * @param ch           Channel
 * @param duty_percent Duty cycle 0-100
 */
void timer_pwm_set_duty(timer_instance_t tim, timer_channel_t ch,
                        uint32_t duty_percent);

/* ---- Microsecond delay ------------------------------------------------- */

/**
 * @brief Blocking microsecond delay using TIM5 in one-pulse mode.
 *
 * TIM5 is a 32-bit timer so delays up to ~4294 seconds are possible.
 * Do NOT use TIM5 for other purposes while this function is in use.
 *
 * @param us Delay in microseconds (0 returns immediately)
 */
void timer_delay_us(uint32_t us);

#endif /* TIMER_H_ */
