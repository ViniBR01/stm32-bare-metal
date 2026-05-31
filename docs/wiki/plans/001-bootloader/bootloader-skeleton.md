# Bootloader Skeleton (Plan 001 Phase 1.5)

This page documents the bootloader skeleton landed by Phase 1.5 — the
on-flash partition layout, the boot sequence, the manual-flash procedure,
and the OpenOCD-based recovery procedure for a bricked board.

The signature-verification path is intentionally absent from this phase;
Phase 1.6 wires `crypto_ecdsa_p256_verify()` into the boot path against the
public key already linked into the bootloader.

## Memory map

```
0x08000000  ┌───────────────────────────────┐  Sector 0,  16 KB
            │ Bootloader (apps/bootloader/  │     linker/bootloader_ls.ld
            │ loader)                       │     never re-flashed by CI
0x08004000  ├───────────────────────────────┤  Sector 1,  16 KB
            │ Reserved — slot metadata      │  Phase 1.7 will populate this
0x0800C000  │ (Phase 1.7)                   │
0x08010000  ├───────────────────────────────┤  Sector 4,  64 KB  (slot A)
            │ img_header_t (140 B)          │     written by sign_image.py
            │ ─────────────                 │
            │ App vector table + .text +    │     linker/app_ls.ld at
            │ .rodata + ...                 │     ORIGIN = SLOT_BASE + 0x8C
0x08020000  ├───────────────────────────────┤  Sector 5, 128 KB
            │ slot A continued              │
0x08040000  ├───────────────────────────────┤  Sector 6+ (slot B in 1.7)
            │ ...                           │
0x08080000  └───────────────────────────────┘  end of flash (512 KB)
```

For Phase 1.5 only sectors 0 (bootloader) and 4-5 (slot A) are touched by
the build system.  Slot B and the metadata sectors are reserved for later
phases.

## Boot sequence

```
Chip reset
   │
   ▼  (NVIC vector base = 0x08000000)
Bootloader Reset_Handler  ─── linker/bootloader_ls.ld
   │   - rcc_init(HSI, 100 MHz)
   │   - uart_init() on USART2 (115200, PA2/PA3)
   │   - read 140 B at 0x08010000
   │   - img_header_parse() — magic + CRC + type checks
   │
   │   parse fails           parse OK
   │   ───────────           ───────
   │   "BL: slot A header    SCB->VTOR = 0x08010000 + payload_offset
   │    parse failed: rc=N"  __set_MSP(*vbase)
   │   slow LED blink loop   jump to *(vbase + 4)
   │                         │
   │                         ▼
   │                       App Reset_Handler  ─── linker/app_ls.ld
   │                         - SCB->VTOR set again by startup (see below)
   │                         - .data / .bss / FPU / NVIC priority init
   │                         - main()
```

Both the bootloader's pre-jump `SCB->VTOR` write and the app's
`Reset_Handler` setting `SCB->VTOR = &_app_vector_base` are intentional.
The first relocates the table for the brief window between the jump and
the app's startup; the second is the canonical, source-controlled write
that the app would perform even if it ran standalone (e.g. flashed
directly with a debugger for development).

## Linker scripts

| Script | Region | Used by |
|---|---|---|
| [linker/bootloader_ls.ld](../../../../linker/bootloader_ls.ld) | sector 0, 16 KB | `apps/bootloader/loader` only |
| [linker/app_ls.ld](../../../../linker/app_ls.ld) | slot at SLOT_BASE+0x8C | every other app |

`app_ls.ld` accepts `SLOT_BASE` via `--defsym` (default `0x08010000`).
[Makefile.common](../../../../Makefile.common) injects the default; an app
or future Phase 1.7 build can override it on the command line.  The script
publishes `_app_vector_base` so the shared startup
([startup/stm32f411_startup.c](../../../../startup/stm32f411_startup.c))
can update `SCB->VTOR` before any interrupt fires.

The bootloader build leaves `_app_vector_base` undefined; startup declares
the symbol weak and skips the VTOR write when it resolves to 0.

## Apps

| App | Linker | Output | Phase 1.5 role |
|---|---|---|---|
| `bootloader` | `bootloader_ls.ld` | `loader.elf` / `loader.bin` | The bootloader image |
| `app_blinky_signed` | `app_ls.ld` | `.signed.bin` | Boot-smoke fixture |
| `cli_simple` | `app_ls.ld` | `.signed.bin` | HIL test target |
| every other app | `app_ls.ld` | `.signed.bin` | Standalone demos |

`make EXAMPLE=bootloader` builds the loader.  Every other app produces a
`<name>.signed.bin` whose first 140 bytes are the `img_header_t` written by
[tools/sign_image.py](../../../../tools/sign_image.py); the bootloader
parses that header before jumping.

## Dev keypair

`make keys` (invoked automatically before any app build) generates a
deterministic ECDSA-P256 keypair from the seed
`stm32-bare-metal-dev-fixture` and writes:

- `build/keys/dev_priv.pem` — the signing private key.  Lives under
  `build/`, which is `.gitignore`d, so the file is regenerated on every
  CI run from the same seed and never committed.
- `build/keys/bootloader_pubkey.c` — the matching public key as a
  `const uint8_t bootloader_pubkey[64]` array.  Linked into the bootloader
  binary; Phase 1.6 will start calling `crypto_ecdsa_p256_verify()`
  against it.

This is a development-only key.  A production pipeline would derive keys
in an HSM/KMS and never expose the seed to a build host
(see [Plan 001 §1.11 production-gap](../001-bootloader-and-security.md)).

## One-time bootloader-flash procedure

CI never programs sector 0.  Each NUCLEO board owned by a developer or by
the HIL rig must have the bootloader flashed once, manually.  Repeat for
every board (`ci` and `dev` ST-LINK serials live in
[scripts/run_hil_tests.py:40-43](../../../../scripts/run_hil_tests.py#L40-L43)).

```sh
make flash-bootloader                          # auto-detect probe
make flash-bootloader BOARD=dev                # pin to dev rig
make flash-bootloader HLA_SERIAL=066CFF...     # pin by ST-LINK serial
```

`make flash-bootloader` is a separate target that **only** programs sector
0 with `loader.elf`; it intentionally has nothing in common with `make
flash` so a typo cannot trash sector 0.  `make flash` itself refuses
when invoked with `EXAMPLE=bootloader` — flashing the bootloader is always
explicit.

The target delegates to
[scripts/flash_bootloader.py](../../../../scripts/flash_bootloader.py), which:

- builds `make EXAMPLE=bootloader` (skip with `BOOTLOADER_FLASH_ARGS=--skip-build`);
- runs OpenOCD with the appropriate `adapter serial` argument;
- reads back the first two words of flash and asserts MSP = `0x20020000`
  and the reset vector falls inside sector 0 with the Thumb bit set, so a
  bad flash fails loudly instead of silently;
- refuses to run when `STM32_BARE_METAL_CI=1` is set in the environment,
  so the HIL CI runner cannot reprogram sector 0 even by mistake.

After this one-time step, every subsequent slot-A image flashed by
`scripts/run_hil_tests.py` (or by `make flash EXAMPLE=<name>` from a dev
machine) goes through the bootloader cleanly.

## Recovery — bricked board

Symptoms: chip won't enumerate, no boot UART output, the previous app
hung, or the bootloader binary was corrupted.

1. **Halt the chip** — ST-LINK can usually attach even if the running code
   is broken, because the bootloader doesn't enable hardware watchdogs.
2. **Mass erase** sector 0, then re-flash the bootloader:
   ```sh
   openocd -f board/st_nucleo_f4.cfg \
     -c "init; reset halt; \
         flash erase_sector 0 0 0; \
         program build/apps/bootloader/loader/loader.elf verify reset exit"
   ```
   `flash erase_sector 0 0 0` erases sector 0 only; if you suspect more
   damage, use `stm32f4x mass_erase 0` instead.
3. Re-flash the slot-A image (`make flash EXAMPLE=<name>` or
   `scripts/run_hil_tests.py`).

If the ST-LINK itself can't connect, hold the BOOT0 pin high during reset
and try again — the chip will then run from the system memory bootloader
(ROM), allowing OpenOCD to attach for the mass erase.

## HIL integration

Existing HIL behaviour is preserved.  The HIL runner now:

1. Builds `cli_simple` with `HIL_TEST=1`, which produces
   `cli_simple.signed.bin` linked at slot A.
2. Flashes the signed bin at `0x08010000` (sector 0 untouched).
3. Resets the board.  The bootloader runs first, prints its own log
   lines, and jumps into cli_simple.  Cli's existing
   `START_TESTS`/`END_TESTS` framing is unaffected; the bootloader's
   `BL: ...` lines are simply ignored by the parser.

A separate boot smoke test ([scripts/run_boot_smoke_test.py](../../../../scripts/run_boot_smoke_test.py))
flashes `app_blinky_signed.signed.bin` and asserts the two boot-log
lines.  CI runs it after the cli_simple HIL job.

## Footguns

- **Two kinds of binaries.**  `loader.bin` lives at sector 0; every other
  `.bin` is linked at slot A.  Mixing them up (e.g. flashing the
  bootloader to slot A, or vice versa) bricks the chain.  The dispatcher
  in `apps/Makefile` plus the `flash`/`flash-bootloader` split makes the
  intended path obvious; double-check by running `arm-none-eabi-objdump
  -h <name>.elf | head -5` and looking at the LMA of `.isr_vector_tbl`.
- **Stale `.signed.bin`.**  Touching `tools/sign_image.py` does not by
  default re-trigger every app's signing step.  When changing the signer
  or the on-flash format, run `make clean && make all`.
- **Forgotten VTOR write.**  Every app linked with `app_ls.ld` exports
  `_app_vector_base`; the shared startup writes it to `SCB->VTOR`
  unconditionally for app builds.  The boot smoke test and the existing
  HIL UART/EXTI tests collectively guard against forgetting this in a
  new linker variant.
