# Anti-Rollback (Plan 001 Phase 1.9)

This page documents the bootloader-side rollback floor and the
fail_count rollback-on-crash path landed by Phase 1.9.

Builds on:
- [verify-and-jump.md](verify-and-jump.md) — Phase 1.6's SHA + ECDSA
  path is unchanged; the floor check sits *after* a successful verify.
- [ab-slots.md](ab-slots.md) — slot pick + fallback decision tree.
  Phase 1.9 reuses the fallback path for both rollback-rejection and
  fail_count-tripped slots.
- [ota.md](ota.md) — OTA receiver wires in the same floor check after
  the post-stream verify, and a new
  `OTA_STATUS_ROLLBACK_REJECTED = 4` is reported on rejection.

## What "rollback floor" means here

Each signed image's header carries `image_version` (a uint32_t set by
`tools/sign_image.py --image-version N`).  The floor is the highest
`image_version` the bootloader has ever booted on this chip.  At boot
time the bootloader rejects any image whose `image_version < floor` —
even if the image is correctly signed.  This stops a real-world
downgrade attack: a signed-but-old image still ships from the same
key, but the chip refuses to run it.

The floor is stored alongside the slot's `monotonic_counter` in
metadata sectors 1 and 2.  Concretely it is computed at every boot as
`max(slot_a.monotonic_counter, slot_b.monotonic_counter)` over
whichever sectors parse cleanly.  See `lib/img/inc/img_header.h` for
the helpers (`img_compute_floor`, `img_compute_new_floor`,
`img_header_meets_floor`).

## Floor location decision

Two reasonable places to keep the floor on this platform:

| Option | Where | Tradeoffs |
|---|---|---|
| **1.  Reuse `monotonic_counter` in slot metadata.** | sectors 1+2 | Simplest.  No new flash regions, no extra writes per boot.  Downside: an attacker with OpenOCD can `mww` the metadata sector to zero the counter and bypass the floor.  A pristine chip's metadata reads as all-FF (parse-fails → floor seeds to 0), so the *first* signed image always boots — fine for dev, worth documenting. |
| **2.  Dedicated counter sector.** | sector 3 (currently reserved per [ab-slots.md](ab-slots.md)) | Survives a metadata wipe; counter stays put even if a debugger nukes sectors 1 and 2.  Costs an extra metadata sector erase + program per floor advance, plus one more sector erase on every fresh-chip seed.  Worth doing when the OpenOCD attack surface gets closed by RDP-1 in [Phase 1.10](#cross-references), not before. |

**Phase 1.9 ships Option 1.**  Sector 3 is reserved for the
dedicated-counter promotion; the upgrade is a reasonable follow-up
once Phase 1.10's RDP-1 closes the OpenOCD-wipe escape hatch.

The threat-model gap is explicit: under Option 1, a chip without
RDP-1 can have its floor wiped via OpenOCD.  That brings the chip
back to "first boot of a pristine chip" — any signed image boots
once, and the floor re-seeds from there.  An attacker can therefore
flash an old signed image *if they already have OpenOCD access*, but
this is the same attacker that can already flash any signed image
they want.  The floor stops downgrades that come in over OTA, which
is the threat the per-image signature does not.

## Boot-time flow (after Phase 1.9)

```
chip reset
   │
   ▼
[OTA magic check]  →  bootloader_ota_run()  (unchanged)
   │
   ▼
read both metadata sectors → compute floor = max(a.mc, b.mc)
   │
   ▼
pick first slot via decision tree (active flag → counter → fallback A)
   │
   ▼
   ┌───── slot tries ─────┐
   │  fail_count >= MAX?  │── yes ──→  next slot
   │  verify_slot()?      │── fail ─→  next slot
   │  meets_floor()?      │── fail ─→  next slot  ("BL: slot X rollback ver=N < floor=M")
   └─────────────────────┘
                  │ all pass
                  ▼
   commit_post_boot_metadata():
     active = 1
     fail_count = increment(prev + 1, clamped at MAX)
     monotonic_counter = max(image_version, floor, prev_counter)
   single erase + program + readback
                  │
                  ▼
              jump to app
```

Important properties:

- **One metadata write per boot.**  Phase 1.7 deferred fail_count
  writes because it would cost the boot path a sector erase that the
  slot-pick logic alone didn't justify.  Phase 1.9 always writes once
  (combining fail_count++, floor advance, and active=1), so the cost
  is paid uniformly and the decision logic is simpler.
- **Floor is monotonic non-decreasing.**  `compute_new_floor` takes
  the max of (existing floor, image_version, prev counter), so even
  if a slot's counter was below the cross-slot max, the next commit
  pulls it up.
- **Pristine-chip seed.**  If both metadata sectors parse-fail
  (all-FF on a fresh chip), `floor=0` and the first signed image
  boots and seeds the floor.  Documented behaviour, not a bug.

## OTA flow on rejection

`apps/bootloader/loader/ota.c` runs the same floor check after a
successful verify (Decision (b) in the original issue: simpler than
threading the candidate version through `OTA_BEGIN`'s payload, at the
cost of "wasting" one OTA cycle's worth of bytes on a downgrade
attempt).

```
OTA_END  →  verify_slot() → meets_floor()? → swap_active_slot()
              │                  │
              │ fail              │ fail
              ▼                  ▼
       STATUS=verify_failed   STATUS=rollback_rejected
       (slot X bytes written, but the previously-active slot's
        metadata is left untouched — no `active=1` ever lands on
        slot X, so the next boot still picks the old image)
```

`OTA_STATUS_ROLLBACK_REJECTED = 4` is the new status value.  Host-side
`tools/_framing.py` and `tools/ota_send.py` already pretty-print
unknown enum values as `unknown(N)`, but the explicit name is wired
through `STATUS_NAMES` so logs read as `rollback_rejected`.

## fail_count semantics (rollback-on-crash)

Originally deferred from Phase 1.7 because the metadata write cost is
~80–120 ms per commit.  Phase 1.9 lands fail_count writes on the same
metadata commit that already advances the floor, so the marginal cost
is one extra write per boot regardless of whether fail_count is
in play.

Lifecycle of `fail_count` on a slot:

```
clean boot:
   bootloader: prev fc=0  → increment to 1 → commit (active=1, fc=1)
   app:        bl_handshake_clear_fail_count() → commit (active=1, fc=0)
                                          (skipped if fc was already 0)
post-init crash:
   chip resets before bl_handshake_clear_fail_count() runs.
   bootloader: prev fc=1 → increment to 2 → commit (active=1, fc=2)
                                            jump
   ... another crash ...
   bootloader: prev fc=2 → increment to 3 → tripped → fall back
```

`IMG_FAIL_COUNT_MAX = 3` — three failed boots in a row mark the slot
as dead.  The clamp at 3 means OpenOCD-injected high values (e.g. 99)
behave identically.

### Cost analysis

| Boot phase | Metadata write | Approx cost |
|---|---|---|
| Bootloader pre-jump commit | 1 (erase 16 KB sector + program 36 B + readback) | ~80–120 ms |
| App post-init clear | 0 if fc was already 0; 1 otherwise | 0 or ~80–120 ms |

**On a clean chip:** every boot pays the bootloader-side commit.
The app-side clear is skipped (fc==0 already), so the steady-state
cost is one commit per boot.

**On the first clean boot after a crash run:** both writes happen —
the bootloader bumps fc and the app immediately resets it.

This is acceptable for a dev board where boots are infrequent.  A
production design would either:
- Use the dedicated-counter sector from Option 2 (one commit becomes
  a 4-byte write, not a sector erase), or
- Skip the app-side clear when the bootloader-committed fc is already
  0 (cheap check via `bl_handshake_clear_fail_count` returning early).

The cheap-check path is already implemented:
`bl_handshake_clear_fail_count` reads the metadata first and skips
the commit when fc is already 0.  No flash wear on a steady-state
clean-boot loop.

## Recovery paths

### A buggy app that resets between init and clear

A pathological app that crashes after `printf_dma_init()` but before
`bl_handshake_clear_fail_count()` walks fail_count up by 1 every boot
until the slot is marked dead at `fc >= 3`.  At that point the
bootloader falls back to the other slot — same path as a verify
failure.

If both slots have apps with the same bug, the bootloader falls back
to the *only* remaining slot, which is also broken; with a verify
failure on that one too, the chip halts with `BL: both slots failed
verify`.

**Recovery:** force OTA mode via the operator-forced recovery path
documented in [ota.md](ota.md):
```
openocd -f board/st_nucleo_f4.cfg \
    -c "init" -c "reset halt" \
    -c "mww 0x40002850 0x4F544131" \
    -c "reset run" -c "exit"
```
Then stream a fixed image via `tools/ota_send.py`.  This is exactly
the same recovery path Phase 1.8 documented; Phase 1.9 doesn't add
any new bricking modes.

### A force-flashed downgrade (OpenOCD attacker)

An attacker who has OpenOCD access can flash an older signed image
into either slot and program metadata that points the bootloader at
it.  The Phase 1.9 floor check rejects this at boot time:

```
BL: rollback ver=1 < floor=2
BL: falling back to slot B
BL: verify ok slot=B in <cycles> ...
```

The chip ends up booting whichever slot still satisfies the floor.
If neither does (both slots got downgraded), the chip halts with
`BL: both slots failed verify`.  This is the threat-model gap that
Phase 1.10 closes via RDP-1: an attacker without OpenOCD cannot
flash anywhere, and an attacker with OpenOCD can wipe but not
downgrade.

## Sign-tool convention

`tools/sign_image.py --image-version N` already stamps `N` into the
header (Phase 1.4).  Phase 1.9 adds no flags; the convention is:

- The dev-build default is `IMAGE_VERSION=1` in every app's Makefile.
- Bump it on every release that should *exclude* prior versions from
  booting after upgrade.
- A future hardening: refuse to sign a version that doesn't strictly
  exceed the previous tag.  Out of scope for this phase; see
  Plan 001 §1.11 (production-gap doc).

## Pieces

| Component | Lives in |
|---|---|
| Pure helpers (`img_header_meets_floor`, `img_compute_floor`, `img_compute_new_floor`, `img_fail_count_increment`/`tripped`, `img_pick_active_slot`, `img_slot_metadata_finalize`) | [lib/img/inc/img_header.h](../../../../lib/img/inc/img_header.h), [lib/img/src/img_header.c](../../../../lib/img/src/img_header.c) |
| Bootloader floor + fail_count flow | [apps/bootloader/loader/main.c](../../../../apps/bootloader/loader/main.c) |
| `verify_slot()` returns the parsed header | [apps/bootloader/loader/verify.{h,c}](../../../../apps/bootloader/loader/verify.h) |
| OTA post-verify floor check | [apps/bootloader/loader/ota.c](../../../../apps/bootloader/loader/ota.c) |
| `OTA_STATUS_ROLLBACK_REJECTED` | [apps/bootloader/loader/ota.h](../../../../apps/bootloader/loader/ota.h) and [tools/_framing.py](../../../../tools/_framing.py) |
| `bl_handshake_clear_fail_count()` helper | [lib/bl_handshake/](../../../../lib/bl_handshake/) |
| HIL test | [scripts/run_anti_rollback_test.py](../../../../scripts/run_anti_rollback_test.py) |
| Host tests (25 new cases) | [tests/lib/img/test_img_header.c](../../../../tests/lib/img/test_img_header.c) |

## Log-line grammar

| Line | Meaning |
|---|---|
| `BL: slot X fc tripped` | The picked slot's fail_count >= IMG_FAIL_COUNT_MAX; treated as failed. |
| `BL: rollback ver=N < floor=M` | Verified slot's image_version is below the floor; treated as failed. |
| `BL: md commit FAIL` | Pre-jump metadata write failed; bootloader continues with the verified image anyway. |
| `OTA: rollback rejected: header_ver=N < floor=M` | OTA receiver rejected a streamed image whose version is below the floor. |
| (host) `STATUS = rollback_rejected` | `tools/ota_send.py` translation of `OTA_STATUS_ROLLBACK_REJECTED`. |

## Sector-0 budget

Phase 1.8 left ~780 bytes of headroom.  Phase 1.9 added the floor
check, the fail_count gate, the `try_boot_slot` helper, and the
`commit_post_boot_metadata` writer.  After lifting `pick_active_slot`
into `lib/img` (deduped between main.c and ota.c) and trimming a
couple of log strings, the bootloader lands at **16352 / 16384 bytes**
— ~32 bytes of headroom.  The post-link `stat` guard in
[apps/bootloader/loader/Makefile](../../../../apps/bootloader/loader/Makefile)
keeps this enforced.

If a future phase needs more budget, the easy levers are: shorter log
strings, sharing `slot_name`/`uart_print_dec32` callsites with OTA, or
moving `commit_post_boot_metadata` into `lib/flash` so the verify-and-
jump hot path can drop the inline copy.

## Cross-references

- [verify-and-jump.md](verify-and-jump.md) — verify path is unchanged;
  Phase 1.9 sits between verify success and jump.
- [ab-slots.md](ab-slots.md) — slot-pick decision tree is now a shared
  `img_pick_active_slot` helper; the "fail_count deferred" disclaimer
  is dropped.
- [ota.md](ota.md) — STATUS table now includes `rollback_rejected`.
- Phase 1.10 (RDP option-byte protection) closes the OpenOCD wipe
  attack on the floor.  Until that lands, a chip without RDP-1 can
  have its floor zeroed by an attacker with debug access — same
  attacker who can already flash any image they want.
