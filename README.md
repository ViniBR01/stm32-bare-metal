# stm32-bare-metal

Bare-metal firmware for the STM32 NUCLEO-F411RE — written from the reset vector up
with **no HAL, no vendor libraries, no IDE, and no dynamic allocation**. Every
peripheral is driven by direct memory-mapped register access, on a stack of layered
drivers, libs, and apps that I built and tested incrementally.

This repo doubles as a learning project and a portfolio piece. It exercises the
hardware/software stack of a real Cortex-M4F system end to end:

- **Boot & runtime:** custom startup, vector table, hardened linker script with
  stack-overflow detection, fault handlers with register dumps, hardware FPU,
  100 MHz PLL clock setup.
- **Drivers:** GPIO, EXTI, SysTick, multi-instance UART (DMA TX + IRQ/DMA RX),
  SPI master on all five instances (polled + DMA), generic DMA, general-purpose
  timers (basic / PWM / µs delay), independent watchdog (IWDG), hardware CRC,
  internal flash read/write — all implemented against the reference manual,
  not a SDK.
- **Middleware (`lib/`):** image-header parser and ECDSA-P256 / SHA-256 crypto
  primitives consumed by the in-flight bootloader track.
- **Apps:** an interactive CLI over UART (DMA-buffered printf, tab completion,
  command history, ISR-safe deferred dispatch) plus standalone peripheral demos.
- **Host tooling:** Python utilities that produce signed firmware images
  (`tools/keygen.py`, `tools/sign_image.py`) using a single source of truth for
  the on-flash format that's mirrored in C.
- **Three-layer test pyramid** (~400+ tests total):
  - **Pure unit tests** — CLI engine, string utils, calc-only helpers (PLL solver,
    baud divisor, PWM prescaler, IWDG timeout solver…).
  - **Driver tests** — host-side, with a fake-peripheral header pattern that
    shadows `stm32f4xx.h` so unmodified driver code runs against in-memory
    register stubs and the test asserts on the resulting register state.
  - **Hardware-in-the-loop** — Unity on the target itself (UART/GPIO/EXTI/SPI
    loopback, FPU, RCC/Timer/SysTick accuracy), with performance baselines and
    regression detection. Cross-language round-trip tests verify the Python
    signing tools against the C parser.
- **CI on every PR:** GitHub Actions runs host tests + cross-app firmware
  builds + a self-hosted Raspberry Pi runner that flashes a real NUCLEO and
  executes the HIL suite. Both Unity and HIL results show up in the GitHub
  Test Summary tab.
- **Agentic development infra:** a Tailscale-fronted MCP server exposes the HIL
  rig to Claude Code so it can build, flash, and test from anywhere; a worktree
  workflow lets multiple agents work on independent issues in parallel without
  touching each other's branches.

## What you can learn from this repo

| Topic | Where to look |
|---|---|
| Driving STM32 peripherals from the reference manual | [drivers/src/](drivers/src/), [docs/wiki/drivers/](docs/wiki/drivers/) |
| Layered, ISR-safe driver design | [docs/wiki/architecture.md](docs/wiki/architecture.md) |
| Testing register-banging code on a host | [docs/wiki/testing.md](docs/wiki/testing.md), [tests/driver_stubs/](tests/driver_stubs/) |
| Pure-function extraction for testability | `*_calc.h` / `*_calc.c` pairs in [drivers/inc/](drivers/inc/) |
| Hardware-in-the-loop testing & perf baselines | [scripts/run_hil_tests.py](scripts/run_hil_tests.py), [docs/wiki/testing.md](docs/wiki/testing.md) |
| Self-hosted CI with a real board | [.github/workflows/ci.yml](.github/workflows/ci.yml), [docs/wiki/ci.md](docs/wiki/ci.md) |
| Image signing & verification (in progress) | [tools/](tools/), [lib/img/](lib/img/), [lib/crypto/](lib/crypto/), [docs/wiki/plans/001-bootloader-and-security.md](docs/wiki/plans/001-bootloader-and-security.md) |
| Multi-agent development workflow | [scripts/worktree_new.sh](scripts/worktree_new.sh), [docs/wiki/agents.md](docs/wiki/agents.md) |

## Active tracks

Multi-phase plans live under [docs/wiki/plans/](docs/wiki/plans/):

- **001 — Bootloader & embedded security** *(in progress)* — custom bootloader at
  sector 0, ECDSA-P256 signed images, A/B slots with rollback-on-fail,
  anti-rollback counter, OTA over UART, RDP option-byte protection. Crypto
  primitives, image format, and host signing tooling have already landed.
- **002 — Inter-board comms + DSP baseband** *(proposed)* — two NUCLEOs talking
  over UART/SPI/I²C with framing, retransmit, and benchmarks; then a software
  BPSK modem with FEC over a wired analog link, with BER-vs-SNR curves.

Driver work and apps are tracked in [docs/wiki/roadmap.md](docs/wiki/roadmap.md).

## Layout

```
drivers/       Peripheral drivers (GPIO, UART, SPI, DMA, Timer, EXTI, SysTick, IWDG, CRC, Flash)
utils/         Reusable utilities (CLI engine, DMA-buffered printf, string utils)
lib/           Middleware libs (no main, no register access) — img, crypto, …
apps/
  basic/       Standalone peripheral demos (blink, button, PWM, IWDG, CRC, …)
  cli/         Interactive CLI app (default build target) + HIL test harness
tools/         Host-side utilities (image signing, future OTA)
tests/         Host unit tests (Unity, native gcc)
scripts/       Repo automation (HIL runner, MCP server, worktrees)
docs/wiki/     Persistent project knowledge base
```

The wiki ([docs/wiki/index.md](docs/wiki/index.md)) is the source of truth for
architecture, drivers, testing, CI, and plans.

## Quick start

```sh
git clone --recurse-submodules https://github.com/ViniBR01/stm32-bare-metal.git
cd stm32-bare-metal

make test                    # host unit tests (no board needed)
make                         # build the default CLI app
make flash                   # flash via OpenOCD (NUCLEO connected over USB)
make serial                  # open the serial console at 115200 baud
```

Type `help` in the CLI prompt to list commands (LED control, SPI throughput
sweep with DWT-cycle-counter timing, uptime, and so on). New commands plug into
the dispatch table in [apps/cli/cli_commands.c](apps/cli/cli_commands.c).

Other useful targets:

```sh
make all                              # build every app
make EXAMPLE=blink_pwm                # build a specific app
make flash EXAMPLE=iwdg_basic         # flash a specific app
make debug EXAMPLE=cli_simple         # OpenOCD + GDB attached
make help                             # full target list
```

### Hardware-in-the-loop

```sh
make clean && make EXAMPLE=cli_simple HIL_TEST=1
make flash EXAMPLE=cli_simple HIL_TEST=1
python3 scripts/run_hil_tests.py
```

The HIL build links Unity onto the target and runs the full on-board suite
(UART/GPIO/EXTI/SPI loopback, FPU, RCC/Timer/SysTick accuracy) over the serial
port, validating perf against checked-in baselines.

## Toolchain

- `arm-none-eabi-gcc` (any reasonably recent GCC; tested with the Ubuntu and
  Homebrew distributions)
- `openocd` for flashing/debugging via the on-board ST-LINK
- Python 3 for the host tools and HIL runner

```sh
sudo apt install gcc-arm-none-eabi openocd python3      # Ubuntu
brew install --cask gcc-arm-embedded && brew install openocd python   # macOS
```

## License

MIT — see [LICENSE](LICENSE).
