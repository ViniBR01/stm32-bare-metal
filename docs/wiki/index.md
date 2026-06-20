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

## Plans

Multi-phase project plans. Each plan's phases are sized as individual GitHub issues.

| Page | Summary |
|---|---|
| [plans/index.md](plans/index.md) | Plans index — convention and active plans |
| [plans/000-repo-refactor.md](plans/000-repo-refactor.md) | Repository refactor — renamed `examples/` → `apps/`, added `lib/` and `tools/`, per-app linker scripts (landed) |
| [plans/001-bootloader-and-security.md](plans/001-bootloader-and-security.md) | Bootloader, image signing, OTA, A/B slots, anti-rollback, RDP |
| [plans/001-bootloader/image-format.md](plans/001-bootloader/image-format.md) | Plan 001 Phase 1.2 — image header & slot metadata on-flash format, CRC-32 spec, parser API |
| [plans/001-bootloader/signing.md](plans/001-bootloader/signing.md) | Plan 001 Phase 1.4 — host signing workflow (`keygen.py`, `sign_image.py`) |
| [plans/001-bootloader/bootloader-skeleton.md](plans/001-bootloader/bootloader-skeleton.md) | Plan 001 Phase 1.5 — bootloader skeleton, slot-A app linker, manual flash + recovery |
| [plans/001-bootloader/verify-and-jump.md](plans/001-bootloader/verify-and-jump.md) | Plan 001 Phase 1.6 — bootloader signature verification (SHA-256 + ECDSA-P256), DWT-timed verify, sector-0 size guard |
| [plans/001-bootloader/ab-slots.md](plans/001-bootloader/ab-slots.md) | Plan 001 Phase 1.7 — A/B slot fallback, dual metadata sectors, lib/flash middleware, SLOT=B build knob, partition_dump.py |
| [plans/001-bootloader/ota.md](plans/001-bootloader/ota.md) | Plan 001 Phase 1.8 — OTA over UART, RTC backup-register entry, framing receiver, atomic active-slot swap, ota_send.py |
| [plans/001-bootloader/anti-rollback.md](plans/001-bootloader/anti-rollback.md) | Plan 001 Phase 1.9 — anti-rollback floor, fail_count rollback-on-crash, bl_handshake helper, OTA STATUS=rollback_rejected |
| [plans/002-comms-and-dsp-baseband.md](plans/002-comms-and-dsp-baseband.md) | Two-board comms (UART/SPI/I²C) + software BPSK modem with FEC |

## Decisions

| Page | Summary |
|---|---|
| [decisions/001-ci-pipeline.md](decisions/001-ci-pipeline.md) | Why GitHub Actions with hosted runners; HIL architecture plan |

## Log

| Page | Summary |
|---|---|
| [log.md](log.md) | Chronological record of significant changes (newest first) |
