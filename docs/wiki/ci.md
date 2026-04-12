# CI Pipeline

## Overview

GitHub Actions workflow at `.github/workflows/ci.yml`.

**Triggers:**
- Every PR targeting `main` (open, reopen, push to PR branch)
- Every push to `main` (post-merge validation)

**Concurrency:** In-progress runs are cancelled when a new commit is pushed to the same branch.

## Jobs

### `host-tests` (active)

| Property | Value |
|---|---|
| Runner | `ubuntu-latest` (GitHub-hosted, free) |
| Required check | Yes — blocks PR merge |

**Steps:**
1. Checkout with `submodules: recursive` (handles SSH→HTTPS URL rewrite for Unity submodule)
2. Print `gcc` and `make` versions
3. `make test`

### `firmware-build` (active)

| Property | Value |
|---|---|
| Runner | `ubuntu-latest` (GitHub-hosted, free) |
| Required check | Yes — add to branch protection after first run on `main` |
| Dependency | none (runs in parallel with `host-tests`) |

**Steps:**
1. Checkout with `submodules: recursive`
2. `apt-get install gcc-arm-none-eabi`
3. Print `arm-none-eabi-gcc --version`
4. `make all` (builds all examples; exits non-zero on any failure)

### `hil-tests` (planned — Issue #86)

| Property | Value |
|---|---|
| Runner | `[self-hosted, pi-hil]` (Raspberry Pi on local server) |
| Required check | Yes — will be added to branch protection when implemented |
| Dependency | `needs: host-tests` |

**Infrastructure ready:** The HIL test harness, machine-parseable output format, automation script (`scripts/run_hil_tests.py`), and performance baselines (`tests/baselines/performance.json`) are all implemented and validated locally. Only the Pi runner registration and CI workflow job addition remain.

## Branch Protection

`main` branch requires `host-tests` to pass before merge. Configured in:
**GitHub → Settings → Branches → Branch protection rules → main**

After PR #94 merges and `firmware-build` runs on `main` for the first time, add `Firmware Build` as a second required status check in the same rule.

When `hil-tests` is added: register it as a third required status check in the same rule.

## Planned Improvements

| Issue | Change |
|---|---|
| ~~#89~~ | ~~Upgrade `actions/checkout` to Node.js 24-compatible version~~ — **Done**: upgraded to `actions/checkout@v6` |
| ~~#85~~ | ~~Add JUnit XML test reporting~~ — **Done**: `tests/unity_to_junit.py` + `dorny/test-reporter@v3` |
| ~~#87~~ | ~~Add `firmware-build` job~~ — **Done**: parallel job, `apt` ARM toolchain, `make all` |
| ~~#88~~ | ~~Add code coverage~~ — **Done**: `lcov` + `genhtml`, uploaded via `actions/upload-artifact@v6` |
| #86 | Add `hil-tests` job — self-hosted Pi runner, OpenOCD flash, serial assertion |

## Adding a New Required Check

1. Add the new job to `ci.yml`
2. Push and open a PR so the check runs once (registers the check name with GitHub)
3. Go to Settings → Branches → edit the `main` rule → add the new check name
