# Driver: SysTick

**Files:** `drivers/inc/systick.h`, `drivers/src/systick.c`

## Purpose

Provides millisecond timing services using the Cortex-M4 SysTick timer:
a persistent tick counter incremented by an ISR every 1 ms, a
non-blocking elapsed-time query, and a blocking delay that polls the
tick counter rather than the hardware COUNTFLAG.

## API

```c
/* Initialise SysTick for 1 ms interrupts. Call after rcc_init(). */
void systick_init(void);

/* Returns milliseconds elapsed since systick_init() was called. */
uint32_t systick_get_ms(void);

/* Returns milliseconds elapsed since start_ms. Handles 32-bit wraparound. */
uint32_t systick_elapsed_since(uint32_t start_ms);

/* Blocking delay of `delay` milliseconds. Returns immediately if delay == 0. */
void systick_delay_ms(uint32_t delay);
```

## Initialisation

`systick_init()` must be called **after** `rcc_init()` so that
`rcc_get_sysclk()` reflects the configured system clock.  It:

1. Sets `SysTick->LOAD = (sysclk / 1000) - 1` (99 999 at 100 MHz).
2. Clears `SysTick->VAL`.
3. Enables the SysTick counter with the processor clock source and the
   TICKINT bit so the ISR fires every 1 ms.
4. Sets the SysTick priority to `IRQ_PRIO_TIMER` (same level as
   general-purpose timer ISRs) via `NVIC_SetPriority(SysTick_IRQn, ...)`.

The tick counter (`s_tick_ms`) is **not** reset by `systick_init()`, so
calling it a second time does not lose elapsed time.

## ISR

`SysTick_Handler()` is defined in `systick.c` as a strong symbol that
overrides the weak alias in the startup file.  It increments the
`volatile uint32_t s_tick_ms` counter by 1 on every tick.

## Non-blocking patterns

```c
/* Pattern: wait up to 500 ms for a condition */
uint32_t start = systick_get_ms();
while (!condition_met()) {
    if (systick_elapsed_since(start) >= 500U) {
        /* timeout */
        break;
    }
}
```

`systick_elapsed_since(start)` uses unsigned subtraction, so it handles
the 32-bit counter wraparound (every ~49.7 days) transparently.

## Blocking delay

`systick_delay_ms(delay)` spins on `systick_elapsed_since()` and returns
immediately when `delay == 0`.  It replaces the previous polled-COUNTFLAG
implementation and is safe to call from any non-ISR context.

## CLI: `uptime` command

The `cli_simple` example registers an `uptime` command that prints the
milliseconds since boot in `hh:mm:ss.mmm` format:

```
> uptime
00:01:23.456
```

## Notes

- Call `systick_init()` once at startup before any call to `systick_get_ms()`.
- The counter is zero before `systick_init()` is called; `systick_get_ms()`
  returns 0 until the first ISR fires.
- For timing intervals shorter than 1 ms, use `timer_delay_us()` from the
  general-purpose timer driver.
- `UNIT_TEST` builds expose `systick_reset_for_test()` to zero the counter
  between test cases.
