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

## Parallel Agent Workflow (Worktrees)

When multiple agents work in parallel, each gets an isolated git worktree with its own
branch. The main working tree is never modified during parallel work.

### When to use
- You are dispatched alongside other agents on independent issues
- The `Agent` tool was invoked with `isolation: "worktree"` (harness handles setup automatically)
- Any time you need a clean branch without affecting the main working tree

### Step-by-step

1. **Confirm or create the GitHub Issue** — every branch needs an issue number.

2. **Create the worktree and branch:**
   ```sh
   bash scripts/worktree_new.sh <issue-number> <short-description>
   # Prints the worktree path, e.g.:
   # Worktree ready: /path/to/stm32-bare-metal-worktrees/115-add-i2c-driver
   ```

3. **Enter the worktree** using the `EnterWorktree` tool with the printed path.
   All work (reads, writes, git commits, make commands) happens inside this directory.

4. **Implement and commit** — commit often with meaningful messages.

5. **Validate locally** — both must pass before pushing:
   ```sh
   make test   # host unit tests
   make all    # all firmware examples
   ```
   Do **not** run HIL tests locally — the physical board is a shared resource. CI handles it.

6. **Push and open the PR:**
   ```sh
   git push -u origin <branch-name>
   gh pr create --title "..." --body "..."
   ```

7. **Report the PR URL to the user and stop.** Do not merge. Do not modify the worktree
   after the PR is open unless asked to address review feedback or CI failures.

8. **Cleanup after merge** (when the user confirms the PR is merged):
   ```sh
   bash scripts/worktree_clean.sh <branch-name>
   ```

See `docs/wiki/agents.md` for parallelism rules, HIL constraints, and troubleshooting.

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

### HIL (Hardware-in-the-Loop) Testing

```sh
make clean && make EXAMPLE=cli_simple HIL_TEST=1   # Build with Unity on target + test harness
make flash EXAMPLE=cli_simple HIL_TEST=1           # Flash HIL firmware
python3 scripts/run_hil_tests.py                   # Automated: build → flash → serial → validate
python3 scripts/run_hil_tests.py --skip-build      # Skip build (use existing binary)
```

**Important:** Always `make clean` when switching between production and `HIL_TEST=1` builds.
The `HIL_TEST` flag changes compiler defines; incremental builds will have stale objects.

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
