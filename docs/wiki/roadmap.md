# Roadmap

## Open Issues by Priority

### Testing & Quality (highest priority)

| Issue | Title | Notes |
|---|---|---|
| #98 | Build driver host test infrastructure | Fake `stm32f4xx.h` + CMSIS stubs. **Prerequisite for all driver tests.** |
| #99 | Driver host tests: GPIO and EXTI | Tier 1 register config tests. Validates infrastructure. |
| #100 | Driver host tests: UART | Tier 1 + Tier 2. Circular buffer wrap logic, baud divisor. |
| #101 | Driver host tests: RCC and Timer | Tier 1 + Tier 2. PLL solver — most complex logic in codebase. |

### CI / Hardware

| Issue | Title | Notes |
|---|---|---|
| ~~#86~~ | ~~Self-hosted Raspberry Pi runner for HIL tests~~ | **Done** — Pi registered with `pi-hil` label, `hil-tests` job added to CI, `needs: host-tests`. |
| #105 | GCC 14 linker compat + HIL script fixes | PR #106 open — `.ARM.exidx` section, throughput formula fix, baseline updates. |

### Architecture / Quality

| Issue | Title | Notes |
|---|---|---|
| #26 | Unified error code scheme | Prerequisite for clean APIs on all new drivers. |
| #73 | Centralise NVIC interrupt priority scheme | Quality gate before adding more interrupt-driven drivers. |

### Driver Development

| Issue | Title | Notes |
|---|---|---|
| #62 | SysTick-based tick counter with non-blocking API | Small. Enables time-based patterns. |
| #69 | Enhance UART: multiple instances + configurable baud rate | High utility. Depends on #26. |
| #66 | Implement I2C master driver | Depends on #26 and #73. |
| #67 | Implement ADC driver | Depends on #26. |
| #68 | Implement IWDG watchdog driver | Depends on #26. |
| #70 | Implement Stop and Standby low-power modes | Depends on #26. |
| #71 | Implement internal flash read/write driver | Depends on #26. |
| #72 | Implement hardware CRC driver | Depends on #26. |

### Examples

| Issue | Title | Notes |
|---|---|---|
| #14 | GPIO input debouncing | Driven by GPIO driver maturity. |
| #16 | Watchdog timer example | Blocked on #68. |
| #22 | Flash memory write/erase example | Blocked on #71. |
| #45 | Large-scale LED array example | Driven by SPI/GPIO driver maturity. |

---

## Suggested Priority Order

1. **#98** — driver test infrastructure (all other driver tests depend on this)
2. **#99** — GPIO/EXTI tests (simplest drivers; validates the new infrastructure)
3. **#100** — UART tests (circular buffer has non-obvious wrap edge cases)
4. **#101** — RCC/Timer tests (PLL solver is the most complex logic in the codebase)
5. **#62** — non-blocking SysTick tick counter
6. **#26** — unified error codes
7. **#69** — multi-instance UART
8. **#73** — NVIC priority scheme
9. Remaining drivers (#66, #67, #68, #70, #71, #72) after #26 and #73
11. Examples (#14, #16, #22, #45) driven by driver availability

---

## Completed Milestones

See [log.md](log.md) for the full history. Key milestones:

- ✅ GPIO, UART, SPI, Timer, DMA, EXTI, SysTick drivers
- ✅ RCC clock configuration (100 MHz via PLL)
- ✅ Hardware FPU enabled in startup
- ✅ Fault handlers with register dump
- ✅ Hardened linker script with stack/heap overflow detection
- ✅ CLI engine with tab completion, command history, ANSI escape handling
- ✅ DMA-buffered printf (printf_dma)
- ✅ Logging system (log_c) with runtime log level control
- ✅ Host unit tests for CLI and string utils (64 tests total)
- ✅ GitHub Actions CI pipeline: `host-tests` + `firmware-build`, branch protection
- ✅ JUnit XML test reporting (Unity → `unity_to_junit.py` → `dorny/test-reporter@v3`)
- ✅ lcov code coverage report uploaded as CI artifact
- ✅ Unity as direct root-level submodule (`3rd_party/unity/`)
- ✅ All GitHub Actions upgraded to Node.js 24
- ✅ HIL test infrastructure: Unity on target, parameterized SPI test sweep (60 tests), machine-parseable output, Python automation script, performance baselines
- ✅ Self-hosted Raspberry Pi HIL runner with `pi-hil` label, `hil-tests` CI job
