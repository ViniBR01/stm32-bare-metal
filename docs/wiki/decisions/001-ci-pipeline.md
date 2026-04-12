# ADR 001: GitHub Actions CI Pipeline

**Date:** 2026-04-11
**Status:** Accepted
**PR:** #83

## Context

The project had no automated CI. Every PR was manually tested locally before merging. This was sufficient for a solo developer but left the door open for regressions if local testing was skipped.

## Decision

Add a GitHub Actions CI pipeline that runs `make test` on every PR targeting `main` and blocks merge if it fails.

## Key choices

### GitHub-hosted runners (not self-hosted) for host tests

**Chosen:** `ubuntu-latest` (free, ephemeral, GitHub-managed).

**Why:** The host tests use only native `gcc` and `make`, both pre-installed on the hosted runner. No setup cost. Free for public repos. Self-hosted runners add maintenance burden (runner software updates, machine uptime) that is not justified until hardware tests are needed.

**Future:** When HIL tests are added (Issue #86), a self-hosted Raspberry Pi runner with label `pi-hil` will be registered. Host tests will stay on the hosted runner.

### Job architecture: host-tests + future hil-tests

The workflow was designed from the start with a second `hil-tests` job in mind:
- `hil-tests` will use `needs: host-tests` — hardware tests only run if host tests pass
- `hil-tests` will use `runs-on: [self-hosted, pi-hil]`
- Both jobs will be required checks in branch protection

### Concurrency cancellation

New commits to a PR cancel the in-progress run for the same branch. This keeps feedback fast and avoids wasting runner minutes on outdated code.

### HIL test frequency (when implemented)

Decision: HIL tests run on every PR (not label-gated, not merge-only). Rationale: the feedback value of hardware tests is highest when they run early, before a PR is approved. If HIL tests become too slow or costly, revisit with label-gating.

### Branch protection

Manual configuration in GitHub Settings (cannot be done via workflow file). Required check: `host-tests` job name. Added after first workflow run registered the check name.

## Consequences

- Every PR now requires `host-tests` to pass before merge
- CI runs take ~12 seconds on the free hosted runner
- Adding a new required check requires both a workflow change and a manual branch protection update
- Node.js 20 deprecation warning in `actions/checkout@v4` — tracked in Issue #89
