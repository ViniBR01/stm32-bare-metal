# Roadmap

## Open Issues by Priority

### Testing & Quality (highest priority)

| Issue | Title | Notes |
|---|---|---|
| #99 | Driver host tests: GPIO and EXTI | Tier 1 register config tests. EXTI validates SYSCFG port mapping + NVIC enable. |
| #100 | Driver host tests: UART | Tier 1 + Tier 2. Circular buffer wrap logic, baud divisor, init register setup. |
| #101 | Driver host tests: RCC and Timer | Tier 1 + Tier 2. PLL solver — most complex logic in codebase. |

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

1. **#99** — GPIO/EXTI driver tests (simplest drivers; validate the fake stub infrastructure)
2. **#100** — UART driver tests (circular buffer has non-obvious wrap edge cases)
3. **#101** — RCC/Timer driver tests (PLL solver is the most complex logic in the codebase)
4. **#62** — non-blocking SysTick tick counter
5. **#26** — unified error codes
6. **#69** — multi-instance UART
7. **#73** — NVIC priority scheme
8. Remaining drivers (#66, #67, #68, #70, #71, #72) after #26 and #73
9. Examples (#14, #16, #22, #45) driven by driver availability

---

## Completed Milestones

See [log.md](log.md) for the full history. Key milestones:

- ✅ GPIO, UART, SPI, Timer, DMA, EXTI, SysTick drivers
- ✅ RCC clock configuration (100 MHz via PLL)
- ✅ Hardware FPU enabled in startup
- ✅ Fault handlers with register dump
- ✅ Hardened linker script with stack/heap overflow detection + GCC 14 `.ARM.exidx` fix
- ✅ CLI engine with tab completion, command history, ANSI escape handling
- ✅ DMA-buffered printf (printf_dma)
- ✅ Logging system (log_c) with runtime log level control
- ✅ Host unit tests: CLI (41), string utils (23), GPIO driver (44) — 108 total
- ✅ Driver host test infrastructure: fake `stm32f4xx.h` + `core_cm4.h` stubs, `test_periph_reset()`
- ✅ GitHub Actions CI pipeline: `host-tests` + `firmware-build` + `hil-tests`, branch protection
- ✅ JUnit XML test reporting (Unity → `unity_to_junit.py` → `dorny/test-reporter@v3`)
- ✅ lcov code coverage report uploaded as CI artifact
- ✅ Unity as direct root-level submodule (`3rd_party/unity/`)
- ✅ All GitHub Actions upgraded to Node.js 24
- ✅ HIL test infrastructure: Unity on target (60 tests), machine-parseable output, Python automation, performance baselines
- ✅ Self-hosted Raspberry Pi HIL runner (`pi-hil` label), `hil-tests` CI job with `needs: host-tests`
