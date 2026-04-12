# Project Log

Chronological record of significant changes. Newest entries at the top.
Format: `## [YYYY-MM-DD] <type> | <title> (<PR/Issue>)`
Types: `merge`, `decision`, `milestone`, `infra`

---

## [2026-04-12] merge | Add host test coverage reporting (#88)

Test Makefiles now accept `EXTRA_CFLAGS`. `tests/Makefile` gains a `coverage` target: clean rebuild with `--coverage`, `lcov --capture`, `lcov --extract '*/utils/src/*'`, `genhtml`. CI installs `lcov` and runs `make -C tests coverage` after tests pass, uploading `tests/coverage-html/` as the `coverage-report` artifact via `actions/upload-artifact@v6` (Node.js 24). Coverage artifacts added to `.gitignore`.

## [2026-04-12] merge | Add firmware-build CI job; update CLAUDE.md pre-push rules (#87)

Added `firmware-build` job to `ci.yml` — installs `gcc-arm-none-eabi` via apt, runs `make all` in parallel with `host-tests`. Catches cross-compilation errors on every PR. Updated `CLAUDE.md` to require both `make test` and `make all` to pass before pushing. `firmware-build` still needs to be added as a required check in branch protection after its first run on `main`.

## [2026-04-12] merge | Make Unity a direct root-level submodule (#84)

Added `3rd_party/unity/` as a direct submodule (ViniBR01/Unity fork, commit 36e9b19), replacing the fragile nested path `3rd_party/log_c/3rd-party/unity/`. Updated both test Makefiles. The Unity copy inside log_c is untouched.

## [2026-04-12] merge | Add JUnit XML test reporting to CI (#85)

Added `tests/unity_to_junit.py` to convert Unity stdout (`file:line:name:PASS/FAIL`) to JUnit XML. Updated `ci.yml` to capture `make test` output with `tee` (preserving exit code via `set -o pipefail`), convert to XML, and publish via `dorny/test-reporter@v3` (Node.js 24). Every PR now shows a Test Summary tab with per-test pass/fail detail.

## [2026-04-12] merge | Upgrade actions/checkout to v6 (Node.js 24) (#89)

Replaced `actions/checkout@v4` (Node.js 20, deprecated) with `actions/checkout@v6.0.2` (Node.js 24). Eliminates the deprecation warning that appeared in every CI run. Resolves before the forced cutover deadline of 2026-06-02.

## [2026-04-12] infra | Add CLAUDE.md and project wiki (#90)

Set up Claude Code project customization. Added `CLAUDE.md` with development workflow rules, build commands, and wiki schema. Created `docs/wiki/` with initial pages covering architecture, roadmap, testing, CI, all drivers, and ADR 001.

## [2026-04-11] merge | Add GitHub Actions CI pipeline (#83)

Created `.github/workflows/ci.yml` with `host-tests` job on `ubuntu-latest`. Runs `make test` on every PR targeting `main` and on every push to `main`. Cancels in-progress runs on new commits. Branch protection on `main` requires `host-tests` to pass. See ADR 001.

## [2026-04-11] merge | Add host unit tests for CLI engine and string utils (#81)

Added Unity-based host unit tests. 41 tests for `utils/src/cli.c` (CLI engine), 23 tests for `utils/src/string_utils.c`. Tests compile with native `gcc` using header stubs for embedded-only includes. Total: 64 tests.

## [2026-02-23] merge | Implement reusable general-purpose timer driver (#80)

Added `drivers/timer.c` covering TIM2–TIM5. Supports basic timer with update interrupt callback, PWM output (mode 1, preload), and `timer_delay_us` using TIM5 in one-pulse mode.

## [2026-02-21] merge | Extract DMA into a reusable generic driver (#79)

Extracted DMA logic from UART/SPI into `drivers/dma.c`. Stream allocation model, transfer-complete and error callbacks, `dma_stream_start_config` fast-path for circular RX.

## [2026-02-21] merge | Harden linker script with stack/heap sections and overflow detection (#78)

Added explicit stack and heap sections to `linker/stm32_ls.ld` with overflow detection symbols. Startup code checks stack canary at boot.

## [2026-02-20] merge | Implement fault handlers with register dump (#77)

Added HardFault, BusFault, UsageFault, MemManage handlers in `drivers/src/fault_handler.c`. On fault: dumps R0–R15, PC, LR, xPSR, and fault status registers over UART.

## [2026-02-20] merge | Enable FPU in startup code (#76)

Enabled the Cortex-M4F hardware FPU (CP10/CP11 coprocessors) in startup code. Added `FPU_ENABLE` build flag (default: 1). Compiler flags: `-mfloat-abi=hard -mfpu=fpv4-sp-d16`.

## [2026-02-18] merge | Enhance GPIO driver with OTYPER, OSPEEDR, PUPDR configuration (#75)

Added `gpio_set_output_type`, `gpio_set_speed`, `gpio_set_pull`, and `gpio_configure_full` to the GPIO driver. Previously only mode and AF were configurable.

## [2026-02-17] merge | Add RCC clock configuration driver with PLL support to reach 100 MHz (#74)

Added `drivers/src/rcc.c`. Configures HSI → PLL → SYSCLK at 100 MHz. APB1 at 50 MHz, APB2 at 100 MHz.

## [2026-02-17] merge | Use DMA to improve SPI throughput in loopback test (#60)

Added `spi_transfer_dma` and `spi_transfer_dma_blocking` to the SPI driver. DMA TX/RX configured per SPI instance.

## [2025-11-05] merge | Add shift register example via SPI (#50)

Added `examples/basic/shift_register_simple` — controls an SN74HC595 shift register using SPI.

## [2025-11-04] merge | Refactor logging module integration (#46–#48)

Switched log_c to a self-contained implementation (no printf dependency, ~1.8 KB). Added `drivers/src/log_platform.c` as the platform integration layer with callback-based backend registration and runtime log level control via `log_platform_set_level()`.

## [2025-10-31] merge | Add PWM example (#44)

Added `examples/basic/blink_pwm` — LED breathing/fade using TIM2 PWM output.

## [2025-10-30] merge | Add timer interrupt example (#43)

Added `examples/basic/timer_interrupt` — TIM2 update interrupt at 1 Hz.
