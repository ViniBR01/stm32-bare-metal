# Plan 000 — Repository Refactor for Multi-Track Work

**Status:** proposed
**Tracking issue:** _(to be filed)_
**Prerequisite for:** [Plan 001 — Bootloader & Security](001-bootloader-and-security.md), [Plan 002 — Inter-Board Comms + DSP Baseband](002-comms-and-dsp-baseband.md)

## Why

Both planned tracks ship multiple firmware images and need reusable, non-driver code (crypto, framing, modem core). Without a refactor, `examples/` becomes a 30-target dumping ground and there's no clean home for middleware that isn't a driver and isn't a leaf utility.

## Goals

1. Make it easy to extend the project with new tracks without restructuring.
2. Keep building, flashing, and testing any old or new app a single command.
3. Give middleware (crypto, framing, modem) a clear home that mirrors how `drivers/` and `utils/` are organised.
4. Allow per-app linker scripts (bootloader needs its own).

## Non-goals

- Changing the driver, utils, or test infrastructure.
- Touching CI workflow logic beyond path updates.
- Replacing Make with CMake or any other build system.

## Target layout

```
stm32-bare-metal/
├── chip_headers/        unchanged
├── linker/              + per-app scripts (bootloader_ls.ld, app_ls.ld) alongside stm32_ls.ld
├── startup/             unchanged
├── 3rd_party/           unchanged
├── drivers/             unchanged
├── utils/               unchanged
│
├── lib/                 NEW — middleware libraries, no main(), no hardware ownership
│   └── (populated by later plans)
│
├── apps/                RENAMED from examples/
│   ├── basic/           current examples/basic/*
│   └── cli/             current examples/cli/
│
├── tests/               unchanged structure; future tests/lib/* added by later plans
├── tools/               NEW — host-side tools (sign_image.py, ota_send.py, ber_plot.py)
├── scripts/             unchanged
└── docs/wiki/plans/     NEW — multi-phase project plans (this doc)
```

## Decisions

| # | Decision | Rationale |
|---|---|---|
| 1 | `examples/` → `apps/` | Matches what they actually are; the term "example" undersells the CLI app. |
| 2 | `lib/` at top level (not nested per-app) | Crypto and framing will be reused across bootloader OTA *and* comms. |
| 3 | App selects linker script via Makefile var | Bootloader needs its own flash region; default stays `stm32_ls.ld`. |
| 4 | Keep flat `EXAMPLE=<name>` lookup | Existing recursive find in `examples/Makefile` already supports nested dirs; no command-line breaking change. |

## Phases

Each phase is one PR.

### Phase 0.1 — Rename `examples/` → `apps/`

**Scope:**
- `git mv examples apps`
- Update `Makefile` (SUBDIRS, $(MAKE) -C examples → apps, find paths under `$(BUILD_DIR)/examples` → `$(BUILD_DIR)/apps`)
- Update `Makefile.common` (`EXAMPLES_DIR` → `APPS_DIR`)
- Update `apps/Makefile` (was `examples/Makefile`) — rename comments
- Update CI workflows that reference `examples/` paths
- Update `CLAUDE.md` and `docs/wiki/architecture.md` directory listings
- Update HIL scripts (`run_hil_tests.py` build path, `mcp_hil_server.py` if any path references)
- Keep variable name `EXAMPLE=` for backward compatibility — internal var only

**Validation:**
- `make test` passes
- `make all` builds all apps
- `make flash EXAMPLE=cli_simple` works
- `python3 scripts/run_hil_tests.py` passes
- CI green on all three jobs (host-tests, firmware-build, hil-tests)

### Phase 0.2 — Introduce `lib/` infrastructure

**Scope:**
- Create empty `lib/` with a `Makefile` that mirrors `drivers/Makefile`
- Each `lib/<name>/` builds to `libname.a` under `$(BUILD_DIR)/lib/<name>/`
- Add `LIB_DIR` to `Makefile.common`, export `<NAME>_LIB` vars as needed
- Add `lib` to root `SUBDIRS`; make it depend on `3rd_party` and `drivers` if later libs need them
- Add a placeholder `lib/.gitkeep` and `lib/README.md` documenting the convention
- Add `tests/lib/` directory with a placeholder for host tests of future libs

**Validation:**
- `make test` and `make all` still pass (no new code, just plumbing)
- Building with empty `lib/` is a no-op

### Phase 0.3 — Per-app linker script selection

**Scope:**
- Add `LDSCRIPT ?= $(LINKER_DIR)/stm32_ls.ld` default (already in `Makefile.common`)
- Allow apps to override `LDSCRIPT` from their own Makefile before linking
- Document the convention in `apps/<app>/Makefile` template
- No new linker scripts yet — those land with bootloader work in Plan 001

**Validation:**
- All existing apps still link with the default script
- A test override (e.g. point `cli_simple` at a copy of the script via a stub var) confirms the mechanism — revert the stub before merge

### Phase 0.4 — Add `tools/` directory

**Scope:**
- Create `tools/` with a `README.md` describing convention (host-side scripts, not built by Make)
- No code yet — populated by Plan 001 (`sign_image.py`, `ota_send.py`) and Plan 002 (`ber_plot.py`)

**Validation:**
- Directory exists, README explains purpose

### Phase 0.5 — Documentation update

**Scope:**
- Refresh `docs/wiki/architecture.md` directory tree
- Refresh `CLAUDE.md` directory tree
- Add `docs/wiki/plans/` to wiki index
- Add a `log.md` entry summarising the refactor

**Validation:**
- Links resolve, tree matches reality

## Risk & rollback

- The rename touches many files but is mechanical. Each phase is its own PR; if Phase 0.1 breaks something subtle in CI, revert is one PR.
- HIL scripts have hardcoded `examples/` paths — sweep with `grep -r examples scripts/ .github/` before merging.
- Documentation drift: phases 0.1 and 0.5 must land together or in adjacent PRs.

## Out of scope

- Replacing Unity, switching test runners, CMake migration — separate proposals if desired.
- Reorganising `drivers/` or `utils/` internally.
