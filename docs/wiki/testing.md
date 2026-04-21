# Testing

## Overview

Tests are split into three categories:

| Type | Location | Toolchain | Runs on |
|---|---|---|---|
| Host unit tests | `tests/` | Native `gcc` | Any Linux/macOS host, CI |
| Hardware-in-the-loop (HIL) | `examples/cli/test_harness.c` | `arm-none-eabi-gcc` | STM32 board + host serial |
| HIL automation | `scripts/run_hil_tests.py` | Python 3 + pyserial | Host (Mac/Linux) |

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
| `exti` | `tests/exti/` | 56 | EXTI driver in `drivers/src/exti_handler.c` |
| `uart` | `tests/uart/` | 46 | UART driver in `drivers/src/uart.c` |
| `rcc` | `tests/rcc/` | 36 | RCC driver in `drivers/src/rcc.c` |
| `timer` | `tests/timer/` | 52 | Timer driver in `drivers/src/timer.c` |
| **Total** | | **298** | |

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

**Implemented extractions:**

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

## Hardware-in-the-Loop (HIL) Testing

### Overview

HIL tests run Unity test cases directly on the STM32 target hardware. They verify real peripheral behaviour, DMA transfers, and performance characteristics that cannot be tested on the host.

### Build flag

The `HIL_TEST` build flag controls inclusion of the test harness and Unity library:

```sh
make EXAMPLE=cli_simple HIL_TEST=1    # ~24 KB — includes Unity + test harness
make EXAMPLE=cli_simple               # ~19 KB — production binary, no test code
```

**Always `make clean` when switching between production and HIL_TEST builds.** The flag changes compiler defines (`-DHIL_TEST_MODE`); Make does not track flag changes.

When `HIL_TEST=1`:
- `-DHIL_TEST_MODE` is added to `CFLAGS`
- Unity include path and `libunity_arm.a` are linked
- `examples/cli/test_harness.c` is compiled (excluded in production builds)
- `run_all_tests` CLI command becomes available
- `spi_perf.c` emits machine-parseable `TEST:` output lines

### Unity on target

Unity is compiled for ARM with `UNITY_OUTPUT_CHAR=_putchar`, routing all Unity output through the project's mpaland printf `_putchar` function (UART). This avoids libc `putchar` which causes Hard Faults on bare-metal (no heap for stdio buffers).

The Unity ARM library is built by `3rd_party/Makefile` and linked as `libunity_arm.a`.

### Test harness

`examples/cli/test_harness.c` contains all HIL test cases. It uses a parameterized macro `RUN_SPI_TEST(instance, prescaler, buffer_size, use_dma)` to run SPI tests across parameter combinations without code duplication.

**Test tiers (80 tests total):**

| Tier | Tests | What it covers |
|---|---|---|
| Tier 1: All-SPI smoke | 10 | All 5 SPI interfaces at max speed (psc=2, 256B), polled + DMA |
| Tier 2a: SPI2 deep sweep | 24 | APB1 bus (50 MHz): all prescalers at 256B + buffer sizes 1/4/16/64B at psc=2 |
| Tier 2b: SPI1 deep sweep | 24 | APB2 bus (100 MHz): same matrix as SPI2 |
| Tier 3: FPU | 2 | Hardware FPU multiplication and division |
| Tier 4: RCC + Timer + SysTick | 8 | Clock frequencies via `rcc_get_*` API; `timer_delay_us` accuracy (±20 µs @ 100 MHz); `systick_get_ms` increments over 5 ms delay; `systick_elapsed_since` over 10 ms delay; `systick_delay_ms(10)` duration within ±1 ms |
| Tier 5: UART loopback | 8 | USART1 (PA9/PB7) + USART6 (PC6/PC7) at 115200 baud, polled, multiple byte patterns |
| Tier 5: GPIO loopback | 2 | Output HIGH/LOW/toggle driving input pin through loopback cable |
| Tier 5: EXTI loopback | 2 | Rising+falling edge ISR via loopback; software trigger via EXTI SWIER |

### Machine-parseable output

`drivers/inc/test_output.h` provides macros for automation:

```
START_TESTS                                               ← sequence start marker
TEST:spi1_dma_psc2_256B:PASS:cycles=4413:throughput_kbps=5801:samples=5:integrity_passes=5
END_TESTS                                                 ← sequence end marker
```

The extended `samples=N:integrity_passes=M` fields are emitted by `spi_perf.c` for multi-sample tests. `run_hil_tests.py` accepts both the legacy single-value format and the extended format.

Test names follow the pattern `spi<N>_<mode>_psc<P>_<S>B` (e.g., `spi2_dma_psc4_256B`).

### Running HIL tests

**Manual (via serial console):**
```sh
make clean && make flash EXAMPLE=cli_simple HIL_TEST=1
make serial
# Type: run_all_tests
```

**Automated (Python script):**
```sh
pip3 install pyserial          # one-time dependency
python3 scripts/run_hil_tests.py
```

The script (`scripts/run_hil_tests.py`) automates the full workflow:
1. `make clean && make EXAMPLE=cli_simple HIL_TEST=1`
2. Flash via OpenOCD
3. Connect to auto-detected serial port
4. Send `run_all_tests`, capture output
5. Parse `TEST:` and Unity result lines
6. Validate metrics against `tests/baselines/performance.json`
7. Exit 0 (pass), 1 (failure/regression), or 2 (error)

Options: `--skip-build`, `--skip-flash`, `--timeout <seconds>`.

### Performance baselines

`tests/baselines/performance.json` stores expected cycle counts and throughput for each SPI test configuration, with tolerance thresholds (typically ±10%, wider for small transfers where overhead dominates).

The HIL runner validates measured values against baselines. Regressions outside tolerance cause exit code 1. See `tests/baselines/README.md` for the update process.

### Remote HIL via MCP tools (Claude Code)

`scripts/mcp_hil_server.py` exposes HIL tests as Claude Code tools via `.mcp.json`. Claude can build, flash, and run tests on the real board autonomously during development.

**Tools available:**

| Tool | Description |
|---|---|
| `hil_status()` | Check Pi reachability and board presence |
| `hil_run_tests(skip_build, skip_flash)` | rsync → build → flash → run → return structured results |

Before each build the server rsyncs the local working tree to the Pi, so uncommitted changes are tested immediately. Requires Tailscale for remote access and `HIL_PI_SSH` env var set to the Pi's Tailscale hostname.

See [hil-remote-access.md](hil-remote-access.md) for full setup instructions.
