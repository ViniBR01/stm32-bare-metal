# A/B Slots and Fallback (Plan 001 Phase 1.7)

This page documents the dual-slot bootloader path landed by Phase 1.7.
Phase 1.6's verify call is reused unchanged; Phase 1.7 only adds the
slot-pick + retry around it.

## Memory map

```
0x08000000  ┌───────────────────────────────┐  Sector 0,  16 KB
            │ Bootloader (apps/bootloader/  │     linker/bootloader_ls.ld
            │ loader)                       │     never re-flashed by CI
0x08004000  ├───────────────────────────────┤  Sector 1,  16 KB
            │ Slot A metadata               │     img_slot_metadata_t (36 B)
            │ (img_slot_metadata_t)         │     written via flash_slot_commit_metadata
0x08008000  ├───────────────────────────────┤  Sector 2,  16 KB
            │ Slot B metadata               │
            │ (img_slot_metadata_t)         │
0x0800C000  ├───────────────────────────────┤  Sector 3,  16 KB
            │ Reserved                      │     anti-rollback log (Phase 1.9)
0x08010000  ├───────────────────────────────┤  Sector 4,  64 KB  (slot A)
            │ img_header_t (140 B)          │     written by sign_image.py
            │ + payload (vectors + .text)   │     linker/app_ls.ld at SLOT_BASE
0x08020000  ├───────────────────────────────┤  Sector 5, 128 KB  (slot A)
            │ slot A continued              │
0x08040000  ├───────────────────────────────┤  Sector 6, 128 KB  (slot B)
            │ Slot B image                  │     same linker, SLOT_BASE=0x08040000
0x08060000  ├───────────────────────────────┤  Sector 7, 128 KB
            │ Reserved                      │
0x08080000  └───────────────────────────────┘  end of flash (512 KB)
```

Each slot is capped at 192 KB by `__slot_size_bytes` in
`linker/app_ls.ld`. Slot A occupies sectors 4 + 5 (192 KB exactly).
Slot B currently occupies sector 6 (128 KB) with sector 7 reserved;
linker caps slot-B builds at 128 KB to keep the door open for an
anti-rollback counter or a slot-history log in the spare sector.

## Slot-pick decision tree

```
read metadata A (sector 1)
read metadata B (sector 2)

A.valid && B.valid:
   A.active && !B.active           → first = A
   B.active && !A.active           → first = B
   both active                     → higher monotonic_counter wins
A.valid only                       → first = A
B.valid only                       → first = B
neither valid (e.g. fresh chip)    → first = A     (skeleton-compatible)

second = the other slot

verify(first):
   ok                              → jump
   fail                            → log "falling back to slot <other>"
                                     verify(second):
                                        ok    → jump
                                        fail  → log "both slots failed"
                                                halt
```

An "all-FF" (erased) metadata sector is treated as **invalid**
because the CRC will not match. This is what Phase 1.5/1.6 hardware
shows on the very first boot: both metadata blobs are absent, so the
bootloader defaults to slot A — exactly the prior behaviour.

## Log-line grammar

| Line | Meaning |
|---|---|
| `BL: stm32-bare-metal bootloader (Phase 1.7)` | Bootloader started |
| `BL: metadata A=<ok\|invalid> B=<ok\|invalid>` | Both metadata blobs read |
| `BL: trying slot <X>` | Active slot picked, about to verify it |
| `BL: slot <X> header parse failed: rc=0xNNNNNNNN` | Header CRC / magic / size / type failed |
| `BL: slot <X> image_type != APP` | Image type wrong |
| `BL: slot <X> verify FAILED: sha mismatch` | SHA-256 of payload != header.sha256 |
| `BL: slot <X> verify FAILED: ecdsa reject` | ECDSA P-256 verify returned 0 |
| `BL: falling back to slot <Y>` | First slot failed; trying the other |
| `BL: verify ok slot=<X> in <N> cycles (~<M> ms)` | Success |
| `BL: jumping to slot <X> @ 0xNNNNNNNN` | About to jump_to_app |
| `BL: both slots failed verify` | Both slots failed — bootloader halts |

`scripts/run_ab_slot_test.py` greps these lines to verify all four
fallback scenarios on hardware.

## SLOT=A / SLOT=B build knob

Every app linked with `linker/app_ls.ld` accepts a slot selector:

```
make EXAMPLE=app_blinky_signed              # SLOT=A → 0x08010000
make EXAMPLE=app_blinky_signed SLOT=B       # SLOT=B → 0x08040000
make EXAMPLE=cli_simple SLOT=B              # any app, slot B
```

Slot-B output paths carry a `_b` suffix so they cannot clobber the
slot-A artefacts:

```
build/apps/bootloader/app_blinky_signed/app_blinky_signed.signed.bin       (SLOT=A)
build/apps/bootloader/app_blinky_signed_b/app_blinky_signed_b.signed.bin   (SLOT=B)
```

`SLOT_BASE`, `SLOT_SUFFIX`, and `SLOT` are exported from
`Makefile.common` for sub-make consumption. Default behaviour
(`SLOT=A`) is bit-identical to pre-Phase-1.7 builds.

## `lib/flash` middleware

Located at `lib/flash/`. The bootloader does **not** link `libflash.a`
yet (it only reads metadata); the middleware exists for the OTA path
that lands in Phase 1.8.

| Function | Purpose |
|---|---|
| `flash_slot_validate_range(addr, len)` | Refuses any range overlapping sector 0 (bootloader). Foundation primitive for any caller that wants a sanity check before invoking `flash_write_*` directly. |
| `flash_slot_erase(slot)` | Erase every sector backing the slot's payload region. |
| `flash_slot_commit_metadata(slot, md)` | Erase the metadata sector, program a packed `img_slot_metadata_t`, read back, compare. Returns `ERR_VERIFY` on byte mismatch. |
| `flash_slot_base_address(slot)` | Returns the absolute payload base. |
| `flash_slot_metadata_address(slot)` | Returns the absolute metadata sector address. |

Power-cut safety: `flash_slot_commit_metadata` erases first, then
writes. An interrupt between the two leaves the sector all-0xFF, which
the parser rejects (CRC mismatch); the bootloader falls back to the
other slot. There is no torn-write window.

Host unit tests cover the validators and address-mapping in
`tests/lib/flash/test_flash_slot.c` (14 tests). The mutating helpers
are exercised on real hardware by the HIL test (the host fake-flash
buffer in `drivers/src/flash.c` covers only sector 0, which isn't
enough to host-test slot-spanning erases).

## `tools/partition_dump.py`

Pretty-prints both metadata blobs and image headers from a connected
board:

```
$ python3 tools/partition_dump.py --hla-serial 066CFF...
=== Slot metadata ===
  Slot A metadata @ 0x08004000
    magic            : 0x534c4f54 ('SLOT')
    metadata_version : 1
    active           : 1
    fail_count       : 0
    monotonic_counter: 7
    ...
  Slot B metadata @ 0x08008000
    INVALID: CRC mismatch
=== Slot images ===
  Slot A image header @ 0x08010000
    magic            : 0x494d4748 ('IMGH')
    image_type       : APP
    payload_size     : 23264
    payload sha256   : a80c41ff... (MATCH)
  ...
```

Two backends: `--backend openocd` (default; live ST-LINK) and
`--backend file` (offline, against a `dump_image` blob). The latter
is handy for archiving a specific board state.

## fail_count semantics — deferred

The Phase 1.7 issue (#158) included a "rollback-on-crash" mechanic:
bootloader increments `fail_count` before jump, app clears it after
init. Implementing that in the bootloader requires a 16 KB sector
erase + reprogram on every boot (~hundreds of milliseconds), which
would more than triple boot time. Combined with the close coupling
to Phase 1.9's `monotonic_counter` write, it makes more sense to
land both in one pass.

Phase 1.7 ships with `fail_count` and `monotonic_counter` already in
the metadata struct and used by the slot-pick algorithm
(`monotonic_counter` is the tiebreaker today), but neither is written
by the bootloader yet. The host can pre-populate them via OpenOCD or
`flash_slot_commit_metadata` on the device side once the OTA path
exists.

## Sector-0 budget

Phase 1.6 left ~5.6 KB of headroom in the 16 KB bootloader sector.
Phase 1.7 added the slot-pick + dual-verify path, growing
`loader.bin` to ~11 700 bytes — about 4.5 KB of headroom remain. The
post-link `stat` guard in
[apps/bootloader/loader/Makefile](../../../../apps/bootloader/loader/Makefile)
keeps this enforced.

## Cross-references

- Phase 1.5: [bootloader-skeleton.md](bootloader-skeleton.md) (the
  unchanged jump path).
- Phase 1.6: [verify-and-jump.md](verify-and-jump.md) (the per-slot
  verify call).
- Phase 1.8: [ota.md](ota.md) — the OTA receiver is the first
  in-product writer of `flash_slot_commit_metadata`; documents the
  active-flag-swap power-cut window referenced by this page's slot-
  pick algorithm.
- Plan 001 overview:
  [001-bootloader-and-security.md](../001-bootloader-and-security.md).
