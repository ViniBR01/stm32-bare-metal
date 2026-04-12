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
│   ├── inc/               Public headers
│   └── src/               Implementations
├── utils/                 Reusable utility libraries
│   ├── inc/               Public headers
│   └── src/               cli.c, printf_dma.c, string_utils.c
├── 3rd_party/             External libraries (git submodules)
│   ├── printf/            Lightweight printf (mpaland/printf fork)
│   └── log_c/             Minimal logging library (contains Unity as nested submodule)
├── examples/              Application firmware examples
│   ├── basic/             Standalone peripheral demos
│   └── cli_app/           Interactive CLI application (default build target)
├── tests/                 Host unit tests (compiled with native gcc, not ARM toolchain)
│   ├── cli/               Tests for utils/src/cli.c
│   └── string_utils/      Tests for utils/src/string_utils.c
├── docs/wiki/             Project knowledge base (this wiki)
├── .github/workflows/     CI pipeline
├── Makefile               Root build orchestrator
└── Makefile.common        Shared toolchain config, flags, common rules
```

## Build System

- **Toolchain:** `arm-none-eabi-gcc`, Cortex-M4 + hard FPU (`-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard`)
- **C standard:** `gnu11`
- **Optimisation:** `-O2 -flto -ffunction-sections -fdata-sections` (dead-code elimination via `--gc-sections`)
- **Hierarchical Makefiles:** Each subdirectory compiles to a static library (`.a`); the top-level Makefile links them.
- **Host tests:** Compiled with native `gcc` (no ARM toolchain needed). Unity framework from `3rd_party/log_c/3rd-party/unity/`.
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
| Unity | `3rd_party/log_c/3rd-party/unity/` | C unit test framework (for host tests) |

> **Note:** Unity is a nested submodule inside log_c. Issue #84 tracks making it a direct root-level submodule.

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
