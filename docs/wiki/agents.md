# Parallel Agent Workflow

This page describes how Claude Code agents use git worktrees to work in parallel
on the same codebase without interfering with each other.

## Overview

Each agent task creates an isolated git worktree — a separate directory on disk with its
own branch, its own build artifacts, and its own working state. The main working tree at
`stm32-bare-metal/` is never modified by agents during parallel work.

All Make targets (`make test`, `make all`, `make clean`) use relative paths, so they work
correctly from any worktree directory without modification.

## Worktree Location

Worktrees live in a dedicated sibling directory alongside the repo:

```
<parent>/
  stm32-bare-metal/                   ← main working tree (user + manual work)
  stm32-bare-metal-worktrees/
    115-add-i2c-driver/               ← agent 1
    116-add-adc-driver/               ← agent 2
    117-fix-uart-baud/                ← agent 3
```

Git does not allow worktrees inside the main working tree, so the sibling directory
approach keeps them grouped and unambiguous.

## Creating a Worktree

```sh
bash scripts/worktree_new.sh <issue-number> <short-description>
```

This script:
1. Fetches the latest `origin/main`
2. Creates a new local branch `<issue-number>-<short-description>` based on it
3. Creates the worktree at `../stm32-bare-metal-worktrees/<branch-name>/`
4. Prints the worktree path and next steps

After running, use `EnterWorktree <path>` in Claude Code to switch into the worktree.

## Agent Workflow

| Step | Action |
|---|---|
| 1 | Confirm or create GitHub Issue — every branch needs an issue number |
| 2 | `bash scripts/worktree_new.sh <N> <desc>` |
| 3 | `EnterWorktree <worktree-path>` |
| 4 | Implement and commit inside the worktree |
| 5 | `make test && make all` — both must pass |
| 6 | `git push -u origin <branch>` |
| 7 | `gh pr create ...` |
| 8 | Report PR URL to the user; stop — do not merge |
| 9 | After user confirms merge: `bash scripts/worktree_clean.sh <branch>` |

**Never modify the main working tree during parallel work.** All reads, writes, git
commits, and make commands happen inside the worktree.

## Parallelism Rules

### Safe to parallelise (no shared resource)
- `make test` — host unit tests, native gcc, fully isolated per worktree
- `make all` — firmware build, arm-none-eabi-gcc, fully isolated per worktree
- All source file editing and git commits

### Must serialise (shared physical device)
- `python3 scripts/run_hil_tests.py --board ci` — CI board, reserved for GitHub Actions
- `python3 scripts/run_hil_tests.py --board dev` — dev board, for agent/manual use
- `make flash` — flashes whichever board is selected; incompatible with concurrent runs

### Two-board setup

Two NUCLEO-F411RE boards are connected to the Pi, each with a dedicated role:

| Board | Who uses it | How to target |
|---|---|---|
| `ci` | GitHub Actions (`hil-tests` job) | `--board ci` (default for CI runner) |
| `dev` | Agents, MCP `hil_run_tests`, SSH sessions | `--board dev` (default for MCP server) |

Because each board has its own ST-LINK serial and OpenOCD instance, the dev board can
run tests while CI is simultaneously running on the ci board — no collision.

### Using the MCP tool (recommended for agents)

The `hil_run_tests` MCP tool automatically targets the **dev board**. Agents should use
it to verify HIL tests without conflicting with CI:

1. Call `hil_status` to confirm the dev board is connected.
2. Call `hil_run_tests()` — rsyncs working tree, builds, flashes dev board, runs tests.
3. Inspect the returned JSON for pass/fail.

This is safe to call at any time, even when a CI run is active on the ci board.

### Running manually via SSH

For ad-hoc testing from an SSH session on the Pi:

```sh
ssh hil-pi
cd ~/stm32-bare-metal
python3 scripts/run_hil_tests.py --board dev --timeout 180
```

### What NOT to do

- Do not run `run_hil_tests.py --board ci` from an agent — that board is reserved for CI.
- Do not run `make flash` without `--board dev` (or the MCP tool) — it defaults to ci.
- Do not run two `--board dev` sessions simultaneously (only one can flash at a time).

## Cleanup After Merge

```sh
bash scripts/worktree_clean.sh <branch-name>
```

Removes the worktree directory, prunes the dangling reference, and deletes the local
branch. Only run after the PR has been merged into `main`.

To inspect all active worktrees at any time:
```sh
git -C /path/to/stm32-bare-metal worktree list
```

## Teams Feature

With `CLAUDE_CODE_EXPERIMENTAL_AGENT_TEAMS=1` enabled, the `Agent` tool accepts
`isolation: "worktree"`. When used this way, the harness creates and enters an isolated
worktree automatically — `worktree_new.sh` is not needed.

The manual script is provided for cases where the agent is invoked without team isolation,
or when the user wants to create a worktree from the command line.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `branch already exists` | Script prints the resume command; run it manually |
| `path already exists` | Check `git worktree list`; prune stale entry with `git worktree prune` |
| `make test` fails in worktree | Confirm you are inside the worktree: run `pwd` |
| Worktree diverged from main | `git -C <worktree> rebase origin/main` — coordinate with user first |
| Stale worktrees after crash | `git worktree prune` removes references to deleted directories |
| HIL tests hang | Do not run HIL from a worktree if CI or another session is also running them |
