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
| Required check | Yes — blocks PR merge |
| Dependency | none (runs in parallel with `host-tests`) |

**Steps:**
1. Checkout with `submodules: recursive`
2. `apt-get install gcc-arm-none-eabi`
3. Print `arm-none-eabi-gcc --version`
4. `make all` (builds all examples; exits non-zero on any failure)

### `hil-tests` (active)

| Property | Value |
|---|---|
| Runner | `[self-hosted, pi-hil]` (Raspberry Pi on local network) |
| Required check | Yes — blocks PR merge |
| Dependency | `needs: host-tests` |

**Steps:**
1. Checkout with `submodules: recursive`
2. Print `arm-none-eabi-gcc --version`
3. `python3 scripts/run_hil_tests.py --timeout 180 --baseline tests/baselines/performance.json --junit-xml hil-test-results.xml` (build → flash → run Unity tests → validate baselines → write JUnit XML)
4. Publish `hil-test-results.xml` via `dorny/test-reporter@v3` (`if: always()`, `fail-on-error: false`) — every PR shows a **HIL Tests** tab in the GitHub Test Summary UI. The XML is written in a `finally` block so it is produced even on serial timeout or build error.

## Branch Protection

`main` requires all three jobs to pass. Configured in:
**GitHub → Settings → Branches → Branch protection rules → main**

Required checks: `Host Tests`, `Firmware Build`, `HIL Tests`.

## Planned Improvements

| Issue | Change |
|---|---|
| ~~#89~~ | ~~Upgrade `actions/checkout` to Node.js 24-compatible version~~ — **Done**: upgraded to `actions/checkout@v6` |
| ~~#85~~ | ~~Add JUnit XML test reporting~~ — **Done**: `tests/unity_to_junit.py` + `dorny/test-reporter@v3` |
| ~~#87~~ | ~~Add `firmware-build` job~~ — **Done**: parallel job, `apt` ARM toolchain, `make all` |
| ~~#88~~ | ~~Add code coverage~~ — **Done**: `lcov` + `genhtml`, uploaded via `actions/upload-artifact@v6` |
| ~~#86~~ | ~~Add `hil-tests` job — self-hosted Pi runner, OpenOCD flash, serial assertion~~ — **Done**: `[self-hosted, pi-hil]`, `needs: host-tests`, `run_hil_tests.py` |
| ~~#123~~ | ~~Add JUnit XML reporting for HIL tests~~ — **Done**: `--junit-xml` flag + `dorny/test-reporter@v3` in `hil-tests` job |

## Adding a New Required Check

1. Add the new job to `ci.yml`
2. Push and open a PR so the check runs once (registers the check name with GitHub)
3. Go to Settings → Branches → edit the `main` rule → add the new check name
