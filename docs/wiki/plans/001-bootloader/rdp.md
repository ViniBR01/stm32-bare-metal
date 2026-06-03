# Readout Protection (Plan 001 Phase 1.10)

This page documents the STM32F4 readout-protection (RDP) story for the
project: what each level does, what option-byte bits flip under the
hood, where RDP fits in the threat model, and how to drive the dev
board into and out of RDP-1 with `scripts/set_rdp.py`.

> **DANGER — RDP Level 2 is permanent.** Once set, there is no path
> back. JTAG/SWD pads are repurposed and the chip can no longer be
> programmed via the debug interface. Do not run `set_rdp.py --level 2`
> on any board you want to keep. The script gates that operation
> behind `RDP_L2_BURN_BOARD=1` for that reason.

## Levels

The numbers below come straight out of RM0383 §3.7 and the STM32F411
reference manual §3.8.1 (option bytes / `FLASH_OPTCR.RDP[7:0]`).

| Level | `RDP[7:0]` | Debug attach | User-flash read | User-flash erase/program | Reversible? |
|---|---|---|---|---|---|
| **0** (factory default) | `0xAA` | yes | yes (any tool) | yes (any tool) | n/a |
| **1** | anything except `0xAA` and `0xCC` | yes (CPU halt OK) | **blocked** while debugger is attached; reads return `0xFF` or fail | only while debugger **detached**; regression to L0 also works (mass-erase) | yes — L1 → L0 **mass-erases user flash** |
| **2** | `0xCC` | **permanently disabled** (JTAG pads repurposed) | n/a | n/a | **no — permanent** |

Concrete read-back semantics on STM32F411 with RDP-1:

- `mdw 0x08000000 4` (OpenOCD, debug attached) → returns `0xFFFFFFFF`
  for every word inside user flash. The CPU still has access; only the
  debug interface is gated.
- `dump_image foo.bin 0x08000000 0x80000` → either fails outright or
  produces an all-`FF` file. Both are acceptable signals that RDP-1
  is engaged. The HIL test (`scripts/run_rdp_test.py`) treats either
  as a pass.
- The bootloader continues to run normally. RDP affects the **debug
  interface**, not CPU-internal accesses.

## Option-byte mechanics

The RDP byte lives inside `FLASH_OPTCR` along with the BOR level, watchdog
type, write-protect bits, and a 16-bit `OPTCR.OPT_LOCK` / `OPTKEYR`
unlock mechanism that mirrors the main flash unlock.

Sequence to change RDP via the debug interface (used by `set_rdp.py`):

1. Halt the CPU.
2. Wait for `FLASH_SR.BSY = 0`.
3. Write `OPTKEY1 = 0x08192A3B` then `OPTKEY2 = 0x4C5D6E7F` to
   `FLASH_OPTKEYR` to clear `OPTCR.OPTLOCK`.
4. Read-modify-write `FLASH_OPTCR`:
   - clear `RDP[7:0]`,
   - OR in the desired RDP byte (`0xAA` for L0, `0xBB` for L1, `0xCC`
     for L2 — anything that isn't `0xAA` or `0xCC` selects L1, but
     `0xBB` is the canonical pick because it survives an
     erase-to-`0xFF` accident).
5. Set `OPTCR.OPTSTRT` to launch the option-byte programming.
6. Poll `FLASH_SR.BSY` until clear.
7. Set `OPTCR.OPTLOCK` to re-arm the lock.
8. Trigger a system reset so the chip latches the new option-byte
   value at boot.

When regressing L1 → L0 the chip performs a mass erase of user flash
**before** the new option byte takes effect. The mass erase clears
sectors 0..7 (the bootloader, both metadata sectors, both slot
regions, and any reserved sectors) — i.e. the entire user-programmable
region. Sector 0 is also wiped, which is why `run_rdp_test.py`
finishes by reflashing the bootloader and the slot-A app from local
build artifacts.

`set_rdp.py` drives this sequence indirectly through OpenOCD's
`stm32f2x options_write` family of commands (which the F4 target
inherits) so we don't have to handcraft the register accesses on each
invocation. Internally OpenOCD does the OPTKEYR dance; we just feed
it the desired RDP level.

## Threat model fit

RDP-1 closes a specific attack: **someone with physical access who
attaches OpenOCD/ST-LINK and reads the chip**. Under L0 they can:

- `dump_image` the bootloader, app images, and metadata sectors —
  exposing the embedded ECDSA public key (not a secret in our threat
  model, but worth noting), the slot-metadata layout, and any data
  cached in user flash.
- `mww` arbitrary words anywhere in user flash. This is the
  load-bearing concern: under L0 an attacker can rewrite the
  `monotonic_counter` in either metadata sector and undo the
  anti-rollback floor that Phase 1.9 will add. Phase 1.10 (this page)
  is what makes that floor meaningful.
- Write a fresh image directly into slot A or slot B without going
  through the OTA path.

What RDP-1 **does not** protect against — explicit non-goals so we
don't oversell the level:

- **Glitch attacks.** Voltage-glitching the moment the bootloader
  writes the RDP byte, or during a mass erase, can land the chip in
  a half-protected state. F411 has no countermeasures.
- **Power analysis / EM side-channels.** ECDSA verify is not
  constant-time on micro-ecc; an attacker with a probe and an
  oscilloscope can leak intermediate values regardless of RDP level.
- **Decapping.** Optical readout of the flash array bypasses RDP
  entirely. Out of scope for the dev-board threat model.
- **The system-memory boot ROM.** Holding BOOT0 high at reset starts
  the ST-supplied UART/USART bootloader. That ROM still functions
  under RDP-1 — but the only thing it can do is regress to L0
  (mass-erase) or read user flash (blocked). It cannot leak the
  contents of a protected chip.
- **`RTC_BKP_DR0..19`.** Backup-domain registers are **not** covered
  by RDP. Phase 1.8 uses `BKP0R` as the OTA-mode flag, which is
  fine because the value is a known, non-secret magic constant. Do
  **not** put signing keys, session secrets, or any other sensitive
  data in `BKP1R..19R`.
- **The system-memory boot ROM's mass-erase path.** That path is
  the very thing that lets an attacker recover a working chip after
  L1 → L0 — but it also wipes user flash, so the leak is "the
  attacker has a blank chip", not "the attacker has your image".

## Operator workflow

```
# Read-only inspection (safe, never writes anything):
python3 scripts/set_rdp.py --status

# Engage RDP-1 on a dev board (debug-attach disabled):
python3 scripts/set_rdp.py --level 1 --confirm

# Regress to L0 (mass-erases user flash, then re-allows debug):
python3 scripts/set_rdp.py --level 0 --confirm

# DANGER: irreversible.  Refuses to run unless RDP_L2_BURN_BOARD=1
# is exported in the environment.  Do not use on a board you intend
# to keep.
RDP_L2_BURN_BOARD=1 python3 scripts/set_rdp.py --level 2 --confirm
```

Both `--level` and `--confirm` are required for any destructive
operation. Running `set_rdp.py` with no flags prints status and
exits 0 — there is no implicit destructive default.

The script honors `STM32_BARE_METAL_CI=1` and refuses to do anything
when the env var is set, the same guard that already protects
sector-0 reflashes. The HIL CI runner exports this variable, so a
typo in a workflow file cannot drop the CI board into RDP-1.

`--board` and `--hla-serial` work the same as `flash_bootloader.py`:
a registered role (`ci`, `dev`) is preferred so the script pins to
the right ST-LINK serial number when both are connected.

## HIL test (`scripts/run_rdp_test.py`)

This test is **not** wired into the standard CI HIL job — it
mass-erases the chip, which would brick every other HIL test that
follows it. It runs only via a manual `workflow_dispatch` GitHub
Actions trigger, on the dev runner, against the dev board.

Flow:

1. `--board ci` is rejected up front. The `BOARD_REGISTRY` lookup
   plus an explicit serial check guarantees the CI board never goes
   through the L1 path.
2. Confirm the chip starts at L0. If the chip is already at L1 or
   L2, the test stops with a clear error rather than blindly
   proceeding.
3. `set_rdp.py --level 1 --confirm`.
4. Attempt `openocd dump_image` of sector 0. Expect either failure
   or all-`FF` content. Either is a pass.
5. Confirm the bootloader still boots normally over UART (the
   bootloader is unaffected by RDP since it is the running CPU
   itself).
6. `set_rdp.py --level 0 --confirm`. Wait for the mass erase to
   finish (poll until `FLASH_SR.BSY = 0`, plus a generous outer
   timeout).
7. Confirm the chip is at L0 and sector 0 reads as `0xFF`.
8. **Recovery step:** reflash the bootloader from
   `build/apps/bootloader/loader/loader.elf`, then reflash the
   slot-A `app_blinky_signed.signed.bin` so the dev rig is left in
   a working state. This is identical to the manual recovery path
   from `bootloader-skeleton.md` — the test just runs it
   automatically.

The test takes ~30 seconds end-to-end; most of it is the L1 → L0
mass-erase (~17 seconds on STM32F411 according to the datasheet,
plus OpenOCD overhead).

## Manual recovery from a stuck state

The risk window during an L1 → L0 regression is the mass erase. If
power is cut, USB is unplugged, or the ST-LINK is reset before the
erase finishes, the option bytes can land in an intermediate state
and the chip becomes unresponsive to OpenOCD until a fresh power
cycle.

Recovery procedure if `set_rdp.py --level 0` was interrupted:

1. Power-cycle the board (unplug USB, plug back in).
2. Hold BOOT0 high during reset to enter the system-memory
   bootloader (the ST-supplied UART one).
3. Use ST-LINK Utility (or another ST-supported tool) from a
   different host to issue a mass erase via the system bootloader.
   This bypasses RDP because it runs from ROM, not user flash.
4. Once the chip is at L0 with empty user flash, re-flash the
   bootloader: `make flash-bootloader`, then re-flash slot A.

If the chip is permanently bricked (e.g. someone ran
`set_rdp.py --level 2 --confirm` with `RDP_L2_BURN_BOARD=1`), there
is no recovery — the chip goes in the bin.

## Cross-references

- [`bootloader-skeleton.md`](bootloader-skeleton.md#recovery--bricked-board)
  — the OpenOCD-reflash recovery path; under RDP-1 the
  reflash-via-OpenOCD step requires an L1 → L0 regression first
  (mass-erases user flash; same outcome as a fresh chip).
- [`ota.md`](ota.md) — the operator-forced recovery via
  `mww 0x40002850 0x4F544131` is **not available** under RDP-1.
  Recovery from a fully-bricked-app state requires regressing to L0
  (mass-erase + reflash) since the OpenOCD `mww` is what RDP-1
  blocks.
- Phase 1.9 (anti-rollback) — the rollback floor lives in user-flash
  metadata. Without RDP-1, an attacker could `mww` it back to zero;
  with RDP-1 they can only mass-erase the chip, which is an obvious
  attack signal and yields a blank board, not a downgraded one.

## Manual GitHub Actions trigger

The RDP HIL test runs from `.github/workflows/rdp-test.yml` via
`workflow_dispatch`. To run it:

1. Open the repo's Actions tab.
2. Select **RDP HIL Test (manual)**.
3. Click **Run workflow**, pick the branch, fill in the `confirm`
   input (must be the literal string `i-understand-this-mass-erases`
   to proceed — a paste-protection guard), and run.

The workflow targets the dev board via `--board dev`, which pins to
the dev ST-LINK serial in `BOARD_REGISTRY`. `run_rdp_test.py`
refuses to run when the resolved serial matches the `ci` entry, so
even a typo in the workflow input cannot drop the CI board into
RDP-1. A future "permanently dev" runner with its own Actions label
can take over if continuous coverage is wanted; today this remains a
manual-on-demand check.
