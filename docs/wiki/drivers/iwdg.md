# IWDG Driver

## Overview

The Independent Watchdog (IWDG) provides a hardware safety mechanism that resets the MCU if software fails to refresh the counter within a configurable timeout period. The IWDG is clocked by the LSI oscillator (~32 kHz) and runs independently of the system clock, making it effective even if the main clock fails.

**Important:** Once started, the IWDG cannot be stopped except by a system reset.

## API

### `iwdg.h`

| Function | Description |
|---|---|
| `iwdg_init(timeout_ms)` | Configure and start the watchdog with the given timeout (1..32768 ms) |
| `iwdg_feed()` | Refresh the counter to prevent reset |
| `iwdg_was_reset_cause()` | Check if the last reset was caused by the IWDG |
| `iwdg_clear_reset_flags()` | Clear all reset cause flags in RCC_CSR |

### `iwdg_calc.h` (pure functions)

| Function | Description |
|---|---|
| `iwdg_compute_config(timeout_ms, lsi_hz, *out)` | Compute prescaler + reload for desired timeout |
| `iwdg_compute_timeout_ms(pr, reload, lsi_hz)` | Compute actual timeout for a given config |
| `iwdg_prescaler_divider(pr)` | Return the divider for a PR register value (0..6 -> /4../256) |

## Timeout Range

At the nominal 32 kHz LSI:

| Prescaler (PR) | Divider | Min timeout (RLR=0) | Max timeout (RLR=4095) |
|---|---|---|---|
| 0 | /4 | 0.125 ms | 512 ms |
| 1 | /8 | 0.25 ms | 1024 ms |
| 2 | /16 | 0.5 ms | 2048 ms |
| 3 | /32 | 1 ms | 4096 ms |
| 4 | /64 | 2 ms | 8192 ms |
| 5 | /128 | 4 ms | 16384 ms |
| 6 | /256 | 8 ms | 32768 ms |

The driver automatically selects the smallest prescaler that can achieve the requested timeout, maximizing timer resolution.

## Registers

- **KR** (Key Register): Write-only. `0x5555` enables write access to PR/RLR, `0xAAAA` reloads the counter, `0xCCCC` starts the watchdog.
- **PR** (Prescaler): 3-bit field selecting /4 through /256.
- **RLR** (Reload): 12-bit down-counter reload value (0..4095).
- **SR** (Status): PVU and RVU bits indicate update-in-progress for PR and RLR.

## Reset Cause Detection

The RCC_CSR register contains `IWDGRSTF` (bit 29), which is set when the IWDG causes a reset. This flag is sticky across resets and must be explicitly cleared by writing `RMVF` to RCC_CSR.

## Testing

- **37 host unit tests** in `tests/iwdg/`
- Pure calc function tests: prescaler divider lookup, timeout computation, config solver with various timeouts, boundary cases, and non-standard LSI frequencies
- Register-level tests: init writes, feed key, reset cause flag reading/clearing

## Example

`examples/basic/iwdg_basic.c` -- initialises the watchdog with a 1-second timeout, feeds it every 500 ms, and detects watchdog resets via LED blink pattern.
