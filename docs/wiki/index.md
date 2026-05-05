# Project Wiki — Index

This is the master index of the project knowledge base.
Claude reads this file at session start via `CLAUDE.md`. Update it whenever a page is added or removed.

## Core

| Page | Summary |
|---|---|
| [architecture.md](architecture.md) | Module map, build system, directory layout, design principles |
| [roadmap.md](roadmap.md) | Open issues by priority, future directions |
| [testing.md](testing.md) | Host unit tests, driver fake-stub testing, HIL on-target testing, coverage |
| [ci.md](ci.md) | CI pipeline, jobs, required checks, how to extend |
| [hil-remote-access.md](hil-remote-access.md) | Tailscale remote SSH setup, SSH key auth, MCP HIL server for Claude Code |
| [agents.md](agents.md) | Parallel agent worktree workflow — creating, using, and cleaning up isolated worktrees |

## Drivers

| Page | Summary |
|---|---|
| [drivers/gpio.md](drivers/gpio.md) | GPIO port/pin configuration, AF, output type, speed, pull |
| [drivers/uart.md](drivers/uart.md) | USART2 driver — DMA TX, interrupt RX, DMA RX, callbacks |
| [drivers/spi.md](drivers/spi.md) | SPI master driver — all 5 instances, polled and DMA transfers |
| [drivers/dma.md](drivers/dma.md) | Generic DMA driver — stream allocation, start/stop, callbacks |
| [drivers/timer.md](drivers/timer.md) | General-purpose timers — basic, PWM, microsecond delay |
| [drivers/systick.md](drivers/systick.md) | SysTick millisecond delay |
| [drivers/exti.md](drivers/exti.md) | External interrupt configuration on GPIO pins |
| [drivers/iwdg.md](drivers/iwdg.md) | Independent Watchdog — configurable timeout, feed, reset cause detection |

## Decisions

| Page | Summary |
|---|---|
| [decisions/001-ci-pipeline.md](decisions/001-ci-pipeline.md) | Why GitHub Actions with hosted runners; HIL architecture plan |

## Log

| Page | Summary |
|---|---|
| [log.md](log.md) | Chronological record of significant changes (newest first) |
