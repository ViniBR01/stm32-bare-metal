# Driver: SysTick

**Files:** `drivers/inc/systick.h`, `drivers/src/systick.c`

## Purpose

Provides a simple millisecond blocking delay using the Cortex-M4 SysTick timer.

## API

```c
void systick_delay_ms(uint32_t delay);
```

## Notes

- Blocking delay — occupies the CPU for the full duration.
- For non-blocking delays or longer intervals, use the general-purpose timer driver (`timer_delay_us` or a timer interrupt callback).
- Used in simple blink and button examples where a blocking delay is acceptable.
