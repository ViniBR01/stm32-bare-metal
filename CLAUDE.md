# STM32 Bare Metal — Claude Instructions

## Development Workflow

Every feature or fix follows this exact sequence:

1. **Check for an existing Issue** — search open issues before creating a new one
2. **Open a GitHub Issue** if none exists, describing the work clearly
3. **Create a branch:** `git checkout -b <issue-number>-<short-description>`
4. **Implement locally** — commit often with meaningful messages
5. **Run `make test` and `make all`** — both must pass before pushing
6. **Push branch and open PR** — only after local validation passes
7. **Verify CI passes** — check both `host-tests` and `firmware-build` GitHub Actions jobs
8. **Do NOT merge** — the user reviews and merges all PRs manually

## Build & Test Commands

```sh
make test                    # Run host unit tests (Unity) — required before any PR
make                         # Build default firmware example (cli_simple)
make EXAMPLE=<name>          # Build a specific example
make all                     # Build all firmware examples
make clean                   # Remove all build artifacts
make flash EXAMPLE=<name>    # Flash to NUCLEO board via OpenOCD
make debug EXAMPLE=<name>    # Flash and attach GDB
make serial                  # Connect to board serial port (115200 baud)
```

## Project Wiki

The wiki is the persistent knowledge base for this project. Read it at the start of any
session involving architecture, drivers, or roadmap decisions.

- @docs/wiki/index.md         — master index of all wiki pages, start here
- @docs/wiki/architecture.md  — module map, build system, design principles
- @docs/wiki/roadmap.md       — open issues, priorities, future directions

## Wiki Maintenance

Update the wiki when work has lasting architectural or knowledge value:

- **New or changed driver/module** → update or create its page in `docs/wiki/drivers/`
- **Architectural decision made** → add an ADR to `docs/wiki/decisions/`
- **Open issues change** → update `docs/wiki/roadmap.md`
- **New wiki page added** → add its entry to `docs/wiki/index.md`
- **Any significant change** → append an entry to `docs/wiki/log.md`

Do NOT update the wiki for small bug fixes or refactors that don't change how future
work should be done.
