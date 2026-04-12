# Testing

## Overview

Tests are split into two categories:

| Type | Location | Toolchain | Runs on |
|---|---|---|---|
| Host unit tests | `tests/` | Native `gcc` | Any Linux/macOS host, CI |
| Hardware-in-the-loop (HIL) | _(future)_ | `arm-none-eabi-gcc` | Raspberry Pi + NUCLEO board |

## Host Unit Tests

### Framework

[Unity](https://github.com/ThrowTheSwitch/Unity) v2.6.2 — lightweight C unit test framework designed for embedded systems.

Unity source lives at `3rd_party/unity/src/` (direct root-level submodule, same fork as used internally by log_c, pinned to the same commit).

### Running tests

```sh
make test        # Build and run all host test suites
```

### Test suites

| Suite | Location | Tests | What is covered |
|---|---|---|---|
| `string_utils` | `tests/string_utils/` | 23 | Custom string functions in `utils/src/string_utils.c` |
| `cli` | `tests/cli/` | 41 | CLI engine in `utils/src/cli.c` |
| `gpio` | `tests/gpio/` | 44 | GPIO driver in `drivers/src/gpio_handler.c` |
| **Total** | | **108** | |

### Architecture

Each suite is a standalone executable compiled with native `gcc`:

```
tests/<suite>/
├── test_<suite>.c      # Unity test file (setUp, tearDown, RUN_TEST macros)
├── stubs/              # Header stubs for embedded-only includes (printf.h, printf_dma.h)
└── Makefile            # Compiles test + source under test + unity.c → test_<suite>.out
```

Stubs allow source files that `#include "printf.h"` or `#include "printf_dma.h"` to compile on the host without the full embedded stack.

### Coverage reporting

Run coverage locally (requires `lcov` and `genhtml`):

```sh
make -C tests coverage      # generates tests/coverage-html/index.html
```

In CI, the HTML report is uploaded as the `coverage-report` artifact on every successful run. Download it from the Actions run page to browse coverage interactively.

The report covers `utils/src/cli.c` and `utils/src/string_utils.c` only — Unity and test files are excluded by `lcov --extract '*/utils/src/*'`.

### Adding a new test suite

1. Create `tests/<module>/` with a `Makefile` modelled on an existing suite
2. Add stubs for any embedded-only headers the module depends on
3. Add the new suite directory to `SUBDIRS` in `tests/Makefile`
4. Write tests following the Unity pattern: `setUp()`, `tearDown()`, `RUN_TEST(test_fn)` in `main()`

### Test output format

Unity outputs one line per test:
```
tests/cli/test_cli.c:45:test_cli_init_registers_commands:PASS
tests/cli/test_cli.c:46:test_process_char_adds_printable:PASS
...
23 Tests 0 Failures 0 Ignored
OK
```

Exit code is non-zero on any test failure, which fails the CI job.

## Driver Testing Strategy

Driver source files mix two kinds of code: pure computation (testable anywhere) and register I/O (requires hardware). Two tiers address these separately.

### Tier 1 — Fake peripheral stubs (register configuration)

The chip headers define peripheral instance macros as fixed hardware addresses (e.g. `#define GPIOA ((GPIO_TypeDef *) 0x40020000)`). A test-only stub layer in `tests/driver_stubs/` shadows the chip header via include path ordering, redirecting these macros to global fake structs in SRAM.

**Include path in driver test Makefiles:**
```makefile
CFLAGS = -I../../tests/driver_stubs   # stubs/ first — shadows chip headers
         -I../../drivers/inc
         -I../../chip_headers/CMSIS/Include
         -I../../chip_headers/CMSIS/Device/ST/STM32F4xx/Include
         -I../../3rd_party/unity/src
```

**`tests/driver_stubs/stm32f4xx.h`** — includes the real `stm32f411xe.h` (for all TypeDefs and bit-flag constants), then overrides all peripheral instances:
```c
#include "stm32f411xe.h"   // real types + bit flags
#include "test_periph.h"   // #undef GPIOA; #define GPIOA (&fake_GPIOA); etc.
```

**`tests/driver_stubs/core_cm4.h`** — stubs all Cortex-M intrinsics:
- `NVIC_EnableIRQ` / `NVIC_DisableIRQ` / `NVIC_SetPriority` — operate on `fake_NVIC` struct
- `__get_PRIMASK()` returns 0; `__disable_irq()` / `__enable_irq()` are no-ops

**Pre-seeding pattern for busy-wait loops:**
Driver functions that poll hardware flags (e.g. `while (!(RCC->CR & RCC_CR_PLLRDY))`) would hang indefinitely in a test. Pre-seed the fake struct before calling:
```c
// Before calling rcc_init():
fake_RCC.CR |= RCC_CR_PLLRDY | RCC_CR_HSERDY;
fake_FLASH.ACR = FLASH_ACR_LATENCY_3WS;
```

**What Tier 1 tests:**
- Register fields after driver init calls (correct bit patterns)
- Which NVIC IRQn numbers got enabled/disabled
- SYSCFG EXTICR port mapping for EXTI
- BSRR / MODER / AFR / BRR values set by GPIO/UART/SPI/Timer drivers

### Tier 2 — Pure function extraction

Complex computation buried in register-writing functions is extracted into standalone pure functions declared in `drivers/inc/<driver>_calc.h`. These take plain values, return plain values, and never touch a register — no mocking needed.

**Convention:** Pure functions live in the existing driver `.c` file but are declared in a companion `<driver>_calc.h` header (not the main public header) to signal they are implementation-level utilities exposed for testing.

**High-value extractions planned:**

| Driver | Pure function | What it tests |
|---|---|---|
| `rcc.c` | `rcc_compute_pll_config(src_hz, target_hz, *out)` | Multi-constraint PLL factor solver |
| `rcc.c` | `rcc_compute_apb_prescaler(hclk, max)` | Prescaler search algorithm |
| `rcc.c` | `rcc_compute_flash_latency(hclk)` | Wait-state lookup table |
| `uart.c` | `uart_compute_baud_divisor(periph_clk, baudrate)` | Integer rounding |
| `uart.c` | `uart_circular_bytes_available(ndtr, last_ndtr, buf_size)` | Wrap-around arithmetic |
| `timer.c` | `timer_compute_pwm_psc(clk, freq, steps)` | PWM prescaler math |
| `timer.c` | `timer_compute_duty_ccr(arr, duty_pct)` | Duty cycle mapping |

### Adding a driver test suite

1. Create `tests/<driver>/` with a `Makefile` that puts `driver_stubs/` first in `-I`
2. For Tier 1: include the driver `.c` source directly in the test executable
3. For Tier 2: include `drivers/inc/<driver>_calc.h` directly
4. Pre-seed any busy-wait flags in `setUp()`; call `test_periph_reset()` in `tearDown()`
5. Add the suite to `SUBDIRS` in `tests/Makefile`

## CI Integration

`make test` is the entry point for CI. See [ci.md](ci.md) for the full pipeline.

Planned improvements:
- ~~Issue #85: JUnit XML output~~ — **Done**: `tests/unity_to_junit.py` converts Unity stdout to JUnit XML; `dorny/test-reporter@v3` publishes a Test Summary tab on every PR
- ~~Issue #88: gcov/lcov coverage reporting~~ — **Done**: `make -C tests coverage` generates HTML via lcov; uploaded as `coverage-report` artifact on every CI run
