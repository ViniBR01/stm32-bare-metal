# Project Plans

Multi-phase project plans. Each plan is a track of related work that spans many issues / PRs. Each plan's phases are sized as individual GitHub issues; the plan doc itself is the durable design + scope record.

## Active plans

| # | Title | Status | Tracking issue |
|---|---|---|---|
| 000 | [Repository refactor for multi-track work](000-repo-refactor.md) | landed | [#136](https://github.com/ViniBR01/stm32-bare-metal/issues/136) |
| 001 | [Bootloader & embedded security](001-bootloader-and-security.md) | in progress | [#137](https://github.com/ViniBR01/stm32-bare-metal/issues/137) |
| 002 | [Inter-board comms + DSP baseband](002-comms-and-dsp-baseband.md) | proposed | [#138](https://github.com/ViniBR01/stm32-bare-metal/issues/138) |

## Convention

- One markdown file per plan, prefixed with a 3-digit number. Numbers are stable; do not renumber.
- Each plan has: **Why**, **End-state vision**, **Phases** (each one issue), **Risk & rollback**, **Out of scope**.
- A plan stays here for life; once finished, status becomes `done` and the doc remains as historical reference.
- Each plan gets a single **tracking issue** in GitHub with a checklist of phase issues. Phase issues are filed as needed, not all up front.
