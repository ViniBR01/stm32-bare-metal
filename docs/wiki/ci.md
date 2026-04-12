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

### `hil-tests` (planned — Issue #86)

| Property | Value |
|---|---|
| Runner | `[self-hosted, pi-hil]` (Raspberry Pi on local server) |
| Required check | Yes — will be added to branch protection when implemented |
| Dependency | `needs: host-tests` |

## Branch Protection

`main` branch requires `host-tests` to pass before merge. Configured in:
**GitHub → Settings → Branches → Branch protection rules → main**

When `hil-tests` is added: register it as a second required status check in the same rule.

## Planned Improvements

| Issue | Change |
|---|---|
| #89 | Upgrade `actions/checkout` to Node.js 24-compatible version (deadline: 2026-06-02) |
| #85 | Add JUnit XML test reporting — GitHub PR Test Summary tab |
| #87 | Add `firmware-build` job — installs `arm-none-eabi-gcc`, runs `make all` |
| #88 | Add code coverage step — gcov/lcov HTML report uploaded as artifact |
| #86 | Add `hil-tests` job — self-hosted Pi runner, OpenOCD flash, serial assertion |

## Adding a New Required Check

1. Add the new job to `ci.yml`
2. Push and open a PR so the check runs once (registers the check name with GitHub)
3. Go to Settings → Branches → edit the `main` rule → add the new check name
