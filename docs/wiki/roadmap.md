# Roadmap

## Open Issues by Priority

### High — Foundational improvements

| Issue | Title | Notes |
|---|---|---|
| ~~#89~~ | ~~Upgrade GitHub Actions to Node.js 24~~ | **Done** — `actions/checkout@v6` merged. |
| #84 | Make Unity a direct project dependency | Unity is currently a submodule-of-submodule. Add as root-level submodule at `3rd_party/unity/`. Unblocks #85 and #88. |
| #87 | Build all firmware examples in CI | Add `firmware-build` job to CI. Parallel to `host-tests`. Catches cross-compilation errors on PRs. |

### Medium — Developer experience

| Issue | Title | Notes |
|---|---|---|
| ~~#85~~ | ~~Add JUnit XML test reporting to CI~~ | **Done** — `unity_to_junit.py` + `dorny/test-reporter@v3`. |
| #88 | Add host test code coverage reporting | Use gcov/lcov. Upload HTML report as CI artifact. Best after #84. |

### Low — Hardware integration

| Issue | Title | Notes |
|---|---|---|
| #86 | Self-hosted Raspberry Pi runner for HIL tests | Register Pi as `pi-hil` runner. Flash via OpenOCD. Capture serial output. Add `hil-tests` CI job with `needs: host-tests`. |

## Future Directions (not yet in issues)

- **More host unit tests:** Current coverage is CLI engine and string utils only. Drivers have no host tests (they require hardware). Coverage of driver logic would require mocking hardware registers.
- **RTOS evaluation:** The project is currently bare-metal super-loop. As complexity grows, a lightweight RTOS (e.g. FreeRTOS) may be worth evaluating.
- **USB CDC:** The NUCLEO board has USB OTG. A USB virtual COM port would remove the dependency on the ST-Link UART bridge.
- **Bootloader:** A custom bootloader would enable firmware updates without OpenOCD.

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
- ✅ GitHub Actions CI pipeline with branch protection on `main`
