# Roadmap

## Open Issues by Priority

### Driver Development

| Issue | Title | Notes |
|---|---|---|
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

1. **#73** — NVIC priority scheme
2. Remaining drivers (#66, #67, #68, #70, #71, #72) after #26 and #73
3. Examples (#14, #16, #22, #45) driven by driver availability

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
- ✅ Host unit tests: CLI (41), string utils (23), GPIO (44), EXTI (56), RCC (36), Timer (52), UART (46) — 298 total
- ✅ SysTick tick counter with non-blocking API: `systick_init()`, `systick_get_ms()`, `systick_elapsed_since()`, `uptime` CLI command, 14 host tests (#62)
- ✅ Driver host test infrastructure: fake `stm32f4xx.h` + `core_cm4.h` stubs, `test_periph_reset()`
- ✅ GitHub Actions CI pipeline: `host-tests` + `firmware-build` + `hil-tests`, branch protection
- ✅ JUnit XML test reporting (Unity → `unity_to_junit.py` → `dorny/test-reporter@v3`)
- ✅ lcov code coverage report uploaded as CI artifact
- ✅ Unity as direct root-level submodule (`3rd_party/unity/`)
- ✅ All GitHub Actions upgraded to Node.js 24
- ✅ HIL test infrastructure: Unity on target (77 tests), machine-parseable output, Python automation, performance baselines
- ✅ Self-hosted Raspberry Pi HIL runner (`pi-hil` label), `hil-tests` CI job with `needs: host-tests`
- ✅ HIL Tier 5: UART loopback (USART1 + USART6), GPIO output/input loopback, EXTI edge + software-trigger tests
- ✅ HIL SPI throughput: 5-sample median with warm-up transfer, majority-vote integrity check, recalibrated baselines (#112)
- ✅ Parallel agent worktree workflow: `worktree_new.sh` / `worktree_clean.sh`, CLAUDE.md instructions, agents wiki page (#114)
- ✅ Tailscale remote access + MCP HIL server (`scripts/mcp_hil_server.py`) for Claude Code integration (#109)
- ✅ Multi-instance UART driver: USART1/2/6, configurable baud rate, hardware table, `uart_init_config()` (#69)
