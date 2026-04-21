# Architecture

## Target Hardware

- **Board:** STM32 NUCLEO-F411RE
- **MCU:** STM32F411RE — Cortex-M4F, 100 MHz (via PLL), 512 KB flash, 128 KB SRAM
- **FPU:** Hardware FPU enabled (fpv4-sp-d16, hard-float ABI)

## Design Philosophy

- **No HAL, no vendor libraries.** All peripheral access is via direct memory-mapped register writes using CMSIS/STM32F4xx headers for register definitions only.
- **No dynamic allocation.** All state is statically allocated.
- **Layered drivers.** Low-level drivers (GPIO, DMA) are reused by higher-level drivers (UART, SPI). Applications use drivers only, never registers directly.
- **ISR-safe architecture.** Interrupt handlers are kept minimal; work is deferred to the main loop where possible (e.g. CLI character input vs command execution).

## Directory Layout

```
stm32-bare-metal/
├── chip_headers/          CMSIS + STM32F4xx device register definitions (read-only reference)
├── linker/                Custom linker script (stm32_ls.ld) with stack/heap overflow detection
├── startup/               Vector table and startup code (stm32f411_startup.c)
├── drivers/               Peripheral drivers (see Drivers section below)
│   ├── inc/               Public headers + test_output.h (HIL machine-parseable output macros)
│   └── src/               Implementations
├── utils/                 Reusable utility libraries
│   ├── inc/               Public headers
│   └── src/               cli.c, printf_dma.c, string_utils.c
├── 3rd_party/             External libraries (git submodules)
│   ├── printf/            Lightweight printf (mpaland/printf fork)
│   ├── log_c/             Minimal logging library (contains Unity as nested submodule)
│   └── unity/             Unity test framework (used for host tests + HIL target tests)
├── examples/              Application firmware examples
│   ├── basic/             Standalone peripheral demos
│   └── cli/               Interactive CLI application (default build target)
│       └── test_harness.c HIL test suite (compiled only with HIL_TEST=1)
├── tests/                 Host unit tests (compiled with native gcc, not ARM toolchain)
│   ├── cli/               Tests for utils/src/cli.c
│   ├── string_utils/      Tests for utils/src/string_utils.c
│   ├── driver_stubs/      Fake peripheral headers shared by all driver test suites
│   ├── gpio/              Tests for drivers/src/gpio_handler.c
│   ├── exti/              Tests for drivers/src/exti_handler.c
│   ├── uart/              Tests for drivers/src/uart.c
│   ├── rcc/               Tests for drivers/src/rcc.c
│   ├── timer/             Tests for drivers/src/timer.c
│   └── baselines/         Performance baseline JSON for HIL regression detection
├── scripts/               Automation scripts
│   ├── run_hil_tests.py   HIL test runner (build → flash → serial → validate)
│   ├── mcp_hil_server.py  MCP server exposing HIL tools to Claude Code
│   ├── worktree_new.sh    Create an isolated git worktree for parallel agent work
│   └── worktree_clean.sh  Remove a merged worktree and its branch
├── docs/wiki/             Project knowledge base (this wiki)
├── .github/workflows/     CI pipeline
├── Makefile               Root build orchestrator
└── Makefile.common        Shared toolchain config, flags, common rules (HIL_TEST flag)
```

## Build System

- **Toolchain:** `arm-none-eabi-gcc`, Cortex-M4 + hard FPU (`-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard`)
- **C standard:** `gnu11`
- **Optimisation:** `-O2 -flto -ffunction-sections -fdata-sections` (dead-code elimination via `--gc-sections`)
- **Linker:** `--specs=nosys.specs` for syscall stubs; `-lc -lm -lgcc` for libc/math/compiler-rt
- **Hierarchical Makefiles:** Each subdirectory compiles to a static library (`.a`); the top-level Makefile links them.
- **Host tests:** Compiled with native `gcc` (no ARM toolchain needed). Unity framework from `3rd_party/unity/`.
- **HIL tests:** `HIL_TEST=1` flag adds `-DHIL_TEST_MODE`, links `libunity_arm.a`, includes `test_harness.c`. Always `make clean` when switching.
- **Log level:** Controlled at compile time via `LOG_LEVEL=LOG_LEVEL_DEBUG` (default: `LOG_LEVEL_INFO`).

## Module Map

```
Application (examples/)
    │
    ├── CLI Engine (utils/cli.c)
    │       └── string_utils (utils/string_utils.c)
    │
    ├── printf_dma (utils/printf_dma.c)
    │       └── UART driver (drivers/uart.c)
    │               └── DMA driver (drivers/dma.c)
    │
    ├── SPI driver (drivers/spi.c)
    │       ├── GPIO driver (drivers/gpio_handler.c)
    │       └── DMA driver (drivers/dma.c)
    │
    ├── Timer driver (drivers/timer.c)
    ├── SysTick driver (drivers/systick.c)
    ├── EXTI driver (drivers/exti_handler.c)
    │       └── GPIO driver
    │
    └── Logging (log_platform.c → log_c → UART driver)
```

## Key Third-Party Dependencies

| Library | Location | Purpose |
|---|---|---|
| CMSIS | `chip_headers/CMSIS/` | Cortex-M4 core definitions |
| STM32F4xx headers | `chip_headers/CMSIS/Device/ST/STM32F4xx/` | Register definitions |
| printf (mpaland) | `3rd_party/printf/` | Lightweight printf, no stdlib dependency |
| log_c | `3rd_party/log_c/` | Minimal levelled logging (~1.8 KB) |
| Unity | `3rd_party/unity/` | C unit test framework (host tests + HIL target tests) |

## Testing Architecture

### Three-layer test pyramid

```
┌─────────────────────────────────────────────┐
│  Layer 3: HIL tests (77 tests)              │  Real board + serial capture
│  Unity on target → assert via UART output   │  Catches hardware-specific bugs
│  SPI sweep, FPU, RCC/Timer accuracy,        │  + performance regression detection
│  UART/GPIO/EXTI loopback                    │
├─────────────────────────────────────────────┤
│  Layer 2: Driver logic tests (298 tests)    │  Host, native gcc
│  Fake peripheral stubs + pure fn extraction │  Catches register config bugs
├─────────────────────────────────────────────┤
│  Layer 1: Pure unit tests (64 tests)        │  Host, native gcc, no mocking
│  CLI engine, string utils                   │  Catches algorithmic bugs
└─────────────────────────────────────────────┘
```

### Layer 2: Fake peripheral stubs mechanism

Every driver uses peripheral instance macros from `stm32f4xx.h` (e.g. `GPIOA`, `RCC`, `USART2`) that resolve to fixed hardware addresses. In a host process, those addresses crash.

The solution: shadow the chip header at the include path level. Driver test Makefiles put `tests/driver_stubs/` **before** `chip_headers/` in their `-I` flags. When a driver `.c` does `#include "stm32f4xx.h"`, the stub is found first.

```
tests/driver_stubs/
├── stm32f4xx.h      ← includes real stm32f411xe.h (for TypeDefs + bit flags),
│                      then #undef/#define all peripheral instance macros to
│                      point at global fake structs in SRAM
├── core_cm4.h       ← stubs NVIC (with inspectable fake struct), SysTick,
│                      and all Cortex-M intrinsics (__get_PRIMASK etc.)
├── test_periph.h    ← declares fake_GPIOA, fake_RCC, fake_USART2 ... (extern)
└── test_periph.c    ← defines all fake struct instances + test_periph_reset()
```

Driver code is **completely unchanged**. Tests pre-seed fake struct fields to simulate hardware state (e.g. set ready flags before init functions that busy-wait), call the driver function, then assert on the fake struct fields.

### Layer 2: Pure function extraction (Tier 2)

For complex computation logic buried in register-writing functions, the logic is extracted into standalone pure functions declared in `drivers/inc/<driver>_calc.h`. These functions take plain values and return plain values — no register access, no mocking required.

Examples:
- `rcc_calc.h`: `rcc_compute_pll_config(src_hz, target_hz, *out)` — PLL factor solver
- `uart_calc.h`: `uart_compute_baud_divisor(periph_clk, baudrate)`, `uart_circular_bytes_available(ndtr, last_ndtr, buf_size)`
- `timer_calc.h`: `timer_compute_pwm_psc(timer_clk, freq, steps)`, `timer_compute_duty_ccr(arr, duty_pct)`

The shell functions (hardware init/control) call the pure functions and apply their results to registers. Tests call the pure functions directly.

### Directory layout (driver tests)

```
tests/
├── cli/               Tests utils/src/cli.c (41 tests)
├── string_utils/      Tests utils/src/string_utils.c (23 tests)
├── driver_stubs/      Fake peripheral headers shared by all driver suites
├── gpio/              Tests drivers/src/gpio_handler.c (44 tests)
├── exti/              Tests drivers/src/exti_handler.c (56 tests)
├── uart/              Tests drivers/src/uart.c (46 tests)
├── rcc/               Tests drivers/src/rcc.c (36 tests)
└── timer/             Tests drivers/src/timer.c (52 tests)
```

## RCC / Clock

- System clock: 100 MHz via PLL (HSI → PLL → SYSCLK)
- APB1: 50 MHz (TIM2–TIM5, SPI2, SPI3, USART2)
- APB2: 100 MHz (SPI1, SPI4, SPI5)
- Configured by `drivers/src/rcc.c` at startup

## Memory Layout (linker script)

- Flash: 0x08000000, 512 KB
- SRAM: 0x20000000, 128 KB
- Stack: top of SRAM, grows downward; overflow detection via stack canary section
- Heap: between BSS end and stack limit (currently unused — no dynamic allocation)
