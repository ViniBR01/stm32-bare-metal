# Project Log

Chronological record of significant changes. Newest entries at the top.
Format: `## [YYYY-MM-DD] <type> | <title> (<PR/Issue>)`
Types: `merge`, `decision`, `milestone`, `infra`

## [2026-04-17] infra | Add JUnit XML reporting for HIL tests in CI (#123)

Extended `scripts/run_hil_tests.py` with a `--junit-xml PATH` argument (default:
`hil-test-results.xml`) and a `write_junit_xml(results, regressions, output_path)`
function. Unity test lines map to `<testcase classname="test_harness">` elements;
`TEST:` sampled performance lines map to `<testcase classname="spi_perf">` elements.
Baseline regressions and integrity failures become `<failure>` child elements with
descriptive messages. The XML is written in a `finally` block so it is always produced,
even on serial timeout or build error, ensuring the report is always available in CI.
Updated `check_baselines` to return regression details alongside the pass/fail bool.
Updated `.github/workflows/ci.yml`: the `hil-tests` job now passes `--junit-xml
hil-test-results.xml` and adds a `Publish HIL test results` step using
`dorny/test-reporter@v3` (`if: always()`, `fail-on-error: false`) — every PR now
shows a **HIL Tests** tab in the GitHub Test Summary UI, mirroring the existing
**Unity Tests** tab from the `host-tests` job.

---

## [2026-04-17] milestone | HIL Tier 4: SysTick hardware tests (#62)

Added three HIL tests to the Tier 4 section of `examples/cli/test_harness.c`:
`test_systick_get_ms_increments` — calls `systick_get_ms()` twice with a
`timer_delay_us(5000)` between them and asserts the difference is 5 ± 1 ms;
`test_systick_elapsed_since` — records a start snapshot, delays 10 ms, and asserts
`systick_elapsed_since(start)` is 10 ± 1 ms; `test_systick_delay_ms_accuracy` —
measures `systick_delay_ms(10)` via DWT cycle counter (expected 1 000 000 cycles at
100 MHz) and asserts within ±100 000 cycles (±1 ms, reflecting inherent 1 ms
quantisation of the tick counter). All three tests pass on hardware (actual
`delay_ms` measurement: ~959 000 cycles). Added `systick_init()` call to
`examples/cli/cli_simple.c` `main()` so the SysTick ISR is running before the
test harness executes. Total HIL tests: 80.

## [2026-04-17] milestone | SysTick tick counter with non-blocking API (#62)

Refactored the SysTick driver from a polled-COUNTFLAG blocking loop to an
ISR-driven millisecond counter. `systick_init()` configures SysTick for 1 ms
interrupts using the processor clock and sets priority to `IRQ_PRIO_TIMER` via
`NVIC_SetPriority(SysTick_IRQn, ...)`. `SysTick_Handler` increments a static
`volatile uint32_t s_tick_ms` counter. `systick_get_ms()` returns the counter
value; `systick_elapsed_since(start)` uses unsigned subtraction for
wraparound-safe elapsed time measurement. `systick_delay_ms()` now polls the
tick counter instead of COUNTFLAG, and returns immediately for delay == 0.
Added `uptime` CLI command that prints boot time as `hh:mm:ss.mmm`.
Added `systick_reset_for_test()` (guarded by `#ifdef UNIT_TEST`) and 14 new
host unit tests in `tests/systick/`. Extended `tests/driver_stubs/core_cm4.h`
with `SysTick_CTRL_*` constants and negative-IRQn handling in
`NVIC_SetPriority`.
## [2026-04-17] milestone | Multi-instance UART driver with configurable baud rate (#69)

Refactored `drivers/src/uart.c` and `drivers/inc/uart.h` to support USART1, USART2,
and USART6. A static hardware descriptor table (`uart_hw_table`) maps each
`uart_instance_t` to its registers, RCC enable bit, GPIO pins (TX/RX), DMA stream IDs
and channels, IRQn, and APB clock getter. New `uart_init_config(const uart_config_t *cfg)`
accepts an instance + baud rate; `uart_init()` is kept unchanged as a USART2/115200
wrapper for backward compatibility with all existing callers (examples, log_platform,
tests). Baud divisor is computed via the correct APB clock for each instance
(`rcc_get_apb1_clk()` for USART2, `rcc_get_apb2_clk()` for USART1/USART6).
Added `fake_USART1` and `fake_USART6` to the driver test stubs. Added 25 new host tests
covering USART1 GPIO pinout, APB2 BRR, NVIC entries, USART6 GPIO pinout, APB2 BRR,
NVIC entries, and `uart_init_config` invalid-argument guards. Total UART tests: 76
(up from 46); total host tests: 328.

## [2026-04-20] milestone | HIL SPI throughput: warm-up run + 5-sample median (#112)

Made HIL SPI performance tests robust against transient loopback corruption and measurement
variance. Two changes: (1) each test now runs 5 back-to-back transfers and reports the
median cycle count, with a majority-vote integrity check (≥4/5 byte-match passes required);
(2) one untimed warm-up transfer runs before the 5 measured samples to pay the one-time
`spi_dma_init_streams()` cost (DMA clock enable, stream CR/PAR config, NVIC setup) outside
the measurement window — all 5 samples then reflect steady-state per-transfer cost, matching
production usage where DMA is initialised once at startup. Extended `TEST:` output format
adds `:samples=N:integrity_passes=M` fields. All 57 baselines recalibrated from warm hardware
runs; small-buffer DMA entries (1B/4B) dropped ~2% vs prior median, confirming the cold first
sample was inflating previous values. Total HIL tests: 73 (SPI/FPU/RCC/Timer/UART unchanged).

## [2026-04-20] milestone | HIL Tier 5: GPIO and EXTI loopback tests (#99)

Added GPIO output/input and EXTI interrupt tests to the HIL harness, reusing the UART
loopback cables already wired on the board (PA9↔PB7 for UART1, PC6↔PC7 for UART6).
GPIO tests: configure one pin as push-pull output and the other as floating input; assert
HIGH, LOW, and toggle propagate through the cable. EXTI tests: arm EXTI line 7 (port B,
PB7) with a minimal `EXTI9_5_IRQHandler` that increments a volatile counter; drive PA9 to
trigger rising and falling edges; assert counter increments; also tests `exti_software_trigger`.
Implemented as 4 consolidated test functions (2 GPIO + 2 EXTI) to minimise serial output and
pin reconfiguration overhead — each function covers all conditions for one loopback pair in a
single init/settle/deinit cycle. Total HIL tests: 77.

## [2026-04-15] milestone | Driver host tests: UART (#100)

Added `tests/uart/` with 46 tests covering the UART driver in `drivers/src/uart.c`.
Tier 1 (register config): `uart_init` CR1/CR2/BRR setup, DMA TX/RX enable, NVIC configuration,
GPIO alternate function pinout for USART2. Tier 2 (pure functions via `uart_calc.h`):
`uart_compute_baud_divisor` rounding at multiple clock/baud combinations; `uart_circular_bytes_available`
wrap-around arithmetic for all cases (no wrap, single wrap, full buffer, empty buffer).
ISR path tests: RXNE callback dispatch, DMA-RX active suppression, error flag handling
(ORE/FE/NF), `uart_clear_errors` reset. Total host tests: 298.

## [2026-04-14] milestone | Driver host tests: RCC and Timer (#101)

Added `tests/rcc/` (36 tests) and `tests/timer/` (52 tests).
RCC Tier 1: `rcc_init` register sequence (HSI→PLL→SYSCLK, AHB/APB prescalers, Flash latency,
PWR voltage scaling), clock getter functions (`rcc_get_sysclk`, `rcc_get_apb1_clk` etc.).
RCC Tier 2 via `rcc_calc.h`: `rcc_compute_pll_config` PLL factor solver across multiple
source/target combinations; `rcc_compute_apb_prescaler`; `rcc_compute_flash_latency` wait-state
lookup. Timer Tier 1: TIM2–TIM5 clock enable, ARR/PSC/CCR register setup for basic, PWM, and
one-pulse modes; NVIC enable/disable paths. Timer Tier 2 via `timer_calc.h`:
`timer_compute_pwm_psc` across frequency/step combinations; `timer_compute_duty_ccr` boundary
cases (0%, 50%, 100%). Total host tests: 252.

---

## [2026-04-17] milestone | Add driver host tests for GPIO and EXTI (#99)

Added `tests/gpio/` (44 tests) and `tests/exti/` (56 tests) using the fake peripheral stub
infrastructure. GPIO tests cover all public API functions: clock enable/disable (RCC AHB1ENR
bits), `gpio_configure_pin` (MODER 2-bit fields), set/clear/toggle (BSRR/ODR), read (IDR),
`gpio_set_af` (AFR nibbles), output type (OTYPER), speed (OSPEEDR), pull (PUPDR), and
`gpio_configure_full` combined call. Also validates port routing (each port maps to its own
fake struct). EXTI tests cover: `exti_configure_gpio_interrupt` (SYSCFG EXTICR port mapping
for all 6 GPIO ports across all 4 EXTICR registers, RTSR/FTSR trigger types, IMR/EMR mode,
NVIC enable via `NVIC_EnableIRQ`), `exti_enable_line`/`exti_disable_line` (NVIC ISER/ICER),
`exti_set_interrupt_mask`/`exti_set_event_mask` (EXTI IMR/EMR), `exti_is_pending`/
`exti_clear_pending` (EXTI PR), and `exti_software_trigger` (EXTI SWIER). Updated
`tests/Makefile` to include the `exti` suite. Total host tests: 164 (CLI + string_utils + GPIO + EXTI).

---

## [2026-04-17] infra | Add parallel agent worktree workflow (#114)

Added `scripts/worktree_new.sh` and `scripts/worktree_clean.sh` for creating and cleaning
isolated git worktrees for parallel agent work. Each worktree gets its own branch based on
`origin/main` and lives at `../stm32-bare-metal-worktrees/<branch>/`. Updated `CLAUDE.md`
with a `## Parallel Agent Workflow (Worktrees)` section covering the full 8-step workflow,
HIL serialisation constraint, and cleanup procedure. Added `docs/wiki/agents.md` with
detailed parallelism rules and troubleshooting.
Compatible with `CLAUDE_CODE_EXPERIMENTAL_AGENT_TEAMS=1` and the `Agent` tool's
`isolation: "worktree"` parameter (harness auto-creates worktrees in that case).

---

## [2026-04-13] infra | Add Tailscale remote access and MCP HIL server (#109)

Added `scripts/mcp_hil_server.py` — a Python stdio MCP server that exposes `hil_status` and
`hil_run_tests` tools to Claude Code. Claude can now autonomously build, flash, and run HIL
tests on the real NUCLEO board during development: it rsyncs the working tree to the Pi (so
uncommitted changes are included), runs `run_hil_tests.py` remotely, and receives structured
JSON results (pass/fail, metrics, regressions). Reuses `parse_test_output` and `load_baselines`
from the existing script without duplication. Registered via `.mcp.json` at project root.
Remote access enabled by Tailscale mesh VPN; configuration via `HIL_PI_SSH` env var.
See `docs/wiki/hil-remote-access.md`.

## [2026-04-13] infra | Add hil-tests CI job on self-hosted Pi runner (#86, #105)

Added `hil-tests` job to `.github/workflows/ci.yml` running on `[self-hosted, pi-hil]`
with `needs: host-tests`. The Pi runner is registered and idle. Also fixed GCC 14 linker
compatibility (`.ARM.exidx` section), throughput calculation truncation (switched to float),
and updated all performance baselines. PR #106.

## [2026-04-12] milestone | HIL test infrastructure with Unity on target (#86)

Implemented hardware-in-the-loop testing framework. Unity compiled for ARM target with `UNITY_OUTPUT_CHAR=_putchar` to route output through UART (avoids libc putchar Hard Fault on bare-metal). Build controlled by `HIL_TEST=1` flag — production builds unchanged (~19 KB), HIL builds ~24 KB. Test harness uses parameterized `RUN_SPI_TEST` macro for 60 tests: all 5 SPI interfaces at max speed (Tier 1), deep prescaler/buffer-size sweep on SPI2 (APB1) and SPI1 (APB2) (Tier 2), plus FPU tests (Tier 3). Machine-parseable output format (`TEST:name:PASS:cycles=N:metric=N`) with `START_TESTS`/`END_TESTS` markers. Python automation script (`scripts/run_hil_tests.py`) handles build → flash → serial capture → parse → baseline validation. Performance baselines stored in `tests/baselines/performance.json` with per-test tolerance thresholds. Key finding: DMA crossover at ~16 bytes (below that, polled is faster due to DMA setup overhead). All infrastructure ready for CI integration — only Pi runner registration remains (#86).

## [2026-04-12] merge | Build driver host test infrastructure + GPIO tests (#98)

Created `tests/driver_stubs/` — a test-only header layer that intercepts `#include "stm32f4xx.h"` via include path ordering, includes the real `stm32f411xe.h` for accurate TypeDefs and bit-flag constants, then overrides all peripheral instance macros to point at global fake structs in SRAM. Companion `core_cm4.h` stub provides fake NVIC (inspectable struct), SysTick, SCB, DWT and stubs for Cortex-M intrinsics. `test_periph_reset()` zeroes all fakes in setUp(). GPIO driver test suite: 44 tests covering clock enable/disable, MODER, BSRR, ODR, IDR, AFR, OTYPER, OSPEEDR, PUPDR and port routing. Driver code is unchanged. Total host tests: 108.

## [2026-04-12] infra | Refresh roadmap + define driver host testing strategy (#97)

Rewrote `roadmap.md` with all 15 open GitHub issues categorised and prioritised. Added "Testing Architecture" section to `architecture.md` documenting the three-layer test pyramid and the fake peripheral stub mechanism. Added "Driver Testing Strategy" section to `testing.md` covering both tiers (fake stubs + pure function extraction) with the include-path override mechanism and pre-seeding pattern. Opened 4 new issues: #98 (infra), #99 (GPIO/EXTI), #100 (UART), #101 (RCC/Timer).

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
