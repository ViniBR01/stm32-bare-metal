# ADR 003 — App target profiles (memory-map selector)

Status: accepted (#167)

## Context

After Plan 001, the project has more than one valid flash memory map:

- The **bootloader map** (Plan 001): a 16 KB bootloader in sector 0, A/B
  application slots at `0x08010000` / `0x08040000`, and every app linked with
  `linker/app_ls.ld`, signed by `tools/sign_image.py` into a `.signed.bin` the
  bootloader parses and verifies.
- The **pre-bootloader map**: a single full-flash image based at `0x08000000`
  (`linker/stm32_ls.ld`), unsigned, flashed directly with a debugger.

Before this change the bootloader map was hard-wired in two places:

1. `Makefile.common` set `LDSCRIPT`, `SLOT`, `SLOT_BASE`, `SLOT_SUFFIX`.
2. Each app Makefile baked in the signing step and assumed slot A.

Two app Makefiles (`apps/cli`, `apps/basic`) were not even slot-aware, so
`make EXAMPLE=cli_simple SLOT=B` silently clobbered the slot-A artifact
(#167). There was no supported way to build any app for the legacy map, and
no clean place to add a future map.

## Decision

Introduce a single **`PROFILE=`** knob in `Makefile.common`. A profile bundles
the four things that pin an app to a memory map:

| Field | Meaning |
|---|---|
| `LDSCRIPT` | which linker script (where the app lands in flash) |
| `SLOT_BASE` | the FLASH ORIGIN `--defsym` fed to a slot-aware script |
| `PROFILE_SUFFIX` | appended to every output path so variants don't clobber |
| `SIGN_IMAGE` | whether `sign_image.py` wraps the `.bin` in an `img_header_t` |

Profiles shipped:

- **`bootloader`** (default) — `app_ls.ld` at `SLOT_BASE` (slot A `0x08010000`
  / slot B `0x08040000` via `SLOT=A|B`), `SIGN_IMAGE=1`. Identical to the
  prior default behaviour.
- **`standalone`** — `stm32_ls.ld` at `0x08000000` full-flash, `SIGN_IMAGE=0`,
  raw unsigned `.bin`/`.elf` flashable directly with a debugger. Suffix
  `_standalone`.

Apps stay **profile-agnostic**: every app Makefile threads `$(PROFILE_SUFFIX)`
through its output paths and gates the signing step on `$(SIGN_IMAGE)`, but
never names a linker script or base address. A new map (e.g. a future
`bootloader_v2`) is added as one more branch in the resolver with no per-app
changes.

```
make EXAMPLE=cli_simple                       # bootloader, slot A (default)
make EXAMPLE=cli_simple SLOT=B                # bootloader, slot B
make EXAMPLE=blink_simple PROFILE=standalone  # legacy 0x08000000, unsigned
```

`SLOT_SUFFIX` is retained as an alias of `PROFILE_SUFFIX` for backward
compatibility.

## Consequences

- #167 is closed: `apps/cli` and `apps/basic` are now slot-aware, so A and B
  builds of the same app coexist (`build/apps/.../<app>_a/...` and
  `.../<app>_b/...`).
- The legacy map is a first-class, tested build path again rather than a
  commented-out relic.
- Every bootloader-profile slot carries a descriptive suffix (`_a` / `_b`), so
  the slot-A artifact path moved from `build/apps/cli/cli_simple/...` to
  `build/apps/cli/cli_simple_a/...`. The HIL / boot-smoke / OTA / verify / RDP /
  anti-rollback scripts that hard-coded the old no-suffix slot-A path were
  updated to the `_a` path in the same change.
- `make flash` / `make debug` are now `PROFILE`-aware (they search for
  `<app>$(PROFILE_SUFFIX).elf`).

## Slot A/B images are position-dependent

A bootloader-profile image **only runs at the slot it was linked for**. A
slot-A image will not run at slot B and vice versa. Two mechanisms bake the
base address in:

1. **Linker ORIGIN.** `app_ls.ld` sets `FLASH ORIGIN = SLOT_BASE + 0x200`, so
   every absolute address the linker resolves (absolute call targets in
   `.text`, `.rodata` pointers, jump tables, the `.data` LMA via `AT> FLASH`)
   is computed for that one base.
2. **Vector table + VTOR.** `.isr_vector_tbl` holds absolute handler addresses
   for the linked base, and `_app_vector_base` (written to `SCB->VTOR` by the
   shared startup) is a link-time constant, not derived at runtime.

This is why the build deliberately produces a separately-linked `_b` artifact
with `SLOT_BASE=0x08040000`. It is correct for the current verify-in-place /
jump-in-place bootloader design — OTA ships the binary built for the
destination slot.

### Making one image slot-agnostic (deferred; future plan)

If a single OTA artifact that either slot can receive ever becomes a
requirement, three approaches exist, in increasing order of cost:

- **Option A — Position-Independent Code (the principled fix).** Build apps
  with `-fPIE` + ROPI/RWPI, link with a PIC-aware script, and fix up the GOT
  and `.data`/`.bss` in startup relative to the runtime load base (read from
  `SCB->VTOR`); the vector table must be made relative or rebuilt in RAM.
  Invasive (per-app flags, linker, startup, likely the image format) with a
  small code-size/speed cost.
- **Option B — runtime VTOR-only relocation.** Insufficient alone: fixes
  interrupt dispatch but not the absolute `.text`/`.rodata` references.
- **Option C — fixed execution region.** Link every app for one fixed address
  and have the bootloader copy the selected slot's payload there before
  jumping; slots become pure storage. Adds a boot-time copy and changes the
  jump + OTA/verify flow.

Decision: keep the per-slot rebuild as the default. Slot-agnostic images would
be a separate plan phase (Option A is the principled route) and are explicitly
out of scope here.

## References

- [Makefile.common](../../../Makefile.common) — the profile resolver.
- [linker/app_ls.ld](../../../linker/app_ls.ld) — bootloader-profile script.
- [linker/stm32_ls.ld](../../../linker/stm32_ls.ld) — standalone-profile script.
- [bootloader-skeleton.md](../plans/001-bootloader/bootloader-skeleton.md) — the
  memory map and boot sequence.
- [ab-slots.md](../plans/001-bootloader/ab-slots.md) — A/B slot layout and the
  `SLOT=A|B` knob.
