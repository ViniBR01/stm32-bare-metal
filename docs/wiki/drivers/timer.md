# Driver: Timer

**Files:** `drivers/inc/timer.h`, `drivers/src/timer.c`

## Purpose

General-purpose timer driver for TIM2–TIM5 on the STM32F411RE. Supports basic timer with update interrupt callback, PWM output, and microsecond delay.

## Instances

| Instance | Counter width | Bus | Notes |
|---|---|---|---|
| `TIMER_2` | 32-bit | APB1 (50 MHz) | Used by `timer_delay_us` (one-pulse mode) |
| `TIMER_3` | 16-bit | APB1 | General use |
| `TIMER_4` | 16-bit | APB1 | General use |
| `TIMER_5` | 32-bit | APB1 | Reserved for `timer_delay_us` |

## API

### Basic timer

```c
void timer_init(timer_instance_t tim, uint32_t prescaler, uint32_t period);
void timer_start(timer_instance_t tim);
void timer_stop(timer_instance_t tim);
void timer_set_period(timer_instance_t tim, uint32_t period);  // change ARR at runtime
void timer_register_callback(timer_instance_t tim, timer_callback_t cb);
    // cb != NULL → enables UIE + NVIC; cb == NULL → disables UIE
```

### PWM output

```c
void timer_pwm_init(timer_instance_t tim, timer_channel_t ch,
                    uint32_t pwm_freq_hz, uint32_t steps);
void timer_pwm_set_duty(timer_instance_t tim, timer_channel_t ch,
                        uint32_t duty_percent);  // 0-100, clamped
```

GPIO alternate-function muxing is the caller's responsibility before calling `timer_pwm_init`.

### Microsecond delay

```c
void timer_delay_us(uint32_t us);  // blocking; uses TIM5 in one-pulse mode
```

> Do NOT use TIM5 for other purposes when using `timer_delay_us`.

## Channels

`TIMER_CH1` .. `TIMER_CH4` correspond to CCR1–CCR4 on the selected timer.

## Example: 1 Hz blink with interrupt

```c
// PSC=49999, ARR=1999 → 50 MHz / 50000 / 2000 = 0.5 Hz... adjust as needed
timer_init(TIMER_2, 49999, 999);   // 1 Hz at 50 MHz APB1 timer clock
timer_register_callback(TIMER_2, my_blink_callback);
timer_start(TIMER_2);
```
