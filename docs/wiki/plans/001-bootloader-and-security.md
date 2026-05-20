# Plan 001 — Bootloader & Embedded Security Track

**Status:** in progress
**Tracking issue:** [#137](https://github.com/ViniBR01/stm32-bare-metal/issues/137)
**Depends on:** [Plan 000 — Repository Refactor](000-repo-refactor.md)

## Why

This track builds the security primitives every embedded platform ships: a custom bootloader, signed firmware images, OTA update, A/B partitioning, anti-rollback, and option-byte protection. It exercises bootloader/runtime separation, reset-vector control, flash partitioning, ECDSA verification on a Cortex-M4, and host-side tooling — all directly transferable industry skills.

Limitations of the F411 platform are part of the learning: no crypto accelerator, no secure-element, no HUK. The project documents what's possible in software, what isn't, and where production designs would require silicon support.

## End-state vision

- Bootloader at `0x08000000` (16 KB, sector 0).
- Two app slots A and B, with metadata sectors describing version/hash/signature.
- Bootloader verifies the signature of the active slot before jumping. Failed verify falls back to the other slot.
- A host tool signs app images with an ECDSA P-256 private key. The corresponding public key is baked into the bootloader.
- A second host tool delivers OTA updates over UART using a framed protocol with CRC and ACK/NACK.
- Anti-rollback: monotonic counter in flash prevents downgrade.
- RDP Level 1 (and a documented experiment with Level 2) demonstrates readout protection.
- Documentation captures threat model, key management, and what would have to change for production.

## Reusable middleware (`lib/`)

| Library | Purpose |
|---|---|
| `lib/crypto/` | SHA-256, ECDSA-P256 verify (micro-ecc), constant-time compare |
| `lib/img/` | Image header struct, slot metadata, integrity + signature checks |
| `lib/framing/` | HDLC-style framing with CRC-16, ACK/NACK, sequence numbers (also reused by Plan 002) |
| `lib/flash/` | Higher-level wrappers over the flash driver: erase-then-write a slot, atomic metadata commit |

## Apps (`apps/bootloader/`)

| App | Purpose |
|---|---|
| `loader` | The bootloader binary. Linker script targets sector 0 only. |
| `app_blinky_signed` | Trivial signed app proving the verify-and-jump path. |
| `app_cli_signed` | Production CLI app, repackaged as signed image. |

## Host tools (`tools/`)

| Tool | Purpose |
|---|---|
| `sign_image.py` | Take a `.bin`, append metadata + signature, produce a flashable image. |
| `keygen.py` | Generate ECDSA P-256 keypair; emit C array for bootloader public key. |
| `ota_send.py` | Stream a signed image to the running device over UART. |
| `partition_dump.py` | Read flash via OpenOCD/`st-info` and pretty-print slot metadata for debugging. |

## Phases

Each phase is one issue / one PR. Phases assume Plan 000 has landed.

### Phase 1.1 — Internal flash driver (Issue #71)

**Status:** done — landed in PR #132 as part of #71. The planned `lib/flash/` middleware (slot erase wrapper, atomic metadata commit, sector-range validator) has been folded into Phase 1.7, where the abstractions are first actually exercised.

Adds `drivers/src/flash.c` with sector erase, word/halfword/byte program, lock/unlock, BSY polling. Host tests use the existing fake-stub pattern.

### Phase 1.2 — Image header & metadata format

**Status:** filed — [#143](https://github.com/ViniBR01/stm32-bare-metal/issues/143)

**Scope:**
- Define `lib/img/img_header.h`: magic, version, size, SHA-256, ECDSA signature, image type
- Define slot metadata format (active flag, fail count, monotonic counter)
- Pure host-testable parser: validate magic, sanity-check size, parse fields
- Document the format in `docs/wiki/plans/001-bootloader/image-format.md`

**Validation:** Host tests cover header round-trip, malformed inputs, boundary values.

### Phase 1.3 — Crypto primitives in `lib/crypto/`

**Status:** filed — [#144](https://github.com/ViniBR01/stm32-bare-metal/issues/144)

**Scope:**
- Vendor micro-ecc (single C file, MIT license)
- Add SHA-256 (existing public-domain implementation, single C file)
- Wrap them in a minimal API: `crypto_sha256(buf, len, out)`, `crypto_ecdsa_verify(pubkey, hash, sig)`
- Host unit tests with NIST FIPS 186-4 test vectors
- Constant-time `crypto_memcmp` for signature comparison

**Validation:** Host tests pass FIPS vectors; ECDSA verify completes in <500 ms on a 100 MHz F411 (measured later in HIL).

### Phase 1.4 — Host tooling: `keygen.py`, `sign_image.py`

**Scope:**
- `keygen.py`: generate P-256 keypair, emit `bootloader_pubkey.c` with `const uint8_t pubkey[64]`
- `sign_image.py`: read `.bin`, compute SHA-256, sign with private key, prepend header, write `.signed.bin`
- Both use Python's `cryptography` library
- Document workflow in `docs/wiki/plans/001-bootloader/signing.md`

**Validation:** Sign a known input, verify with `openssl` CLI as a cross-check.

### Phase 1.5 — Bootloader skeleton (no crypto yet)

**Scope:**
- New `apps/bootloader/loader/` with `linker/bootloader_ls.ld` (16 KB at 0x08000000)
- App linker script `linker/app_ls.ld` (slot A at 0x08010000, configurable)
- Bootloader: minimal init (clock, UART for logs), read header from slot A, sanity check magic, jump to app reset vector
- App template: relocatable to slot offset, vector table at app base via `SCB->VTOR`
- Trivial `app_blinky_signed` that blinks (signature ignored at this stage)

**Validation:** HIL test that bootloader → app jump works end-to-end. App's blink LED toggles. Reset re-enters bootloader cleanly.

### Phase 1.6 — Signature verification + verify-and-jump

**Scope:**
- Bootloader includes embedded public key
- Computes SHA-256 over app payload, verifies ECDSA against header signature
- On verify success: jump. On failure: log and stop (no fallback yet).
- Time the verify with the cycle counter; log the result.

**Validation:** Signed image boots; tampered byte fails verify and bootloader halts. HIL test asserts both cases via UART output. Verify time recorded as a baseline.

### Phase 1.7 — A/B slots and fallback

**Scope:**
- `lib/flash/` middleware (absorbed from former Phase 1.1 scope): slot-erase wrapper, atomic metadata commit primitive, sector-range validator
- Slot metadata in dedicated sector with active flag, fail counter
- Bootloader picks active slot, increments fail counter before jump, app must clear it after init (rollback-on-crash)
- If active slot verify fails, try the other slot; if both fail, halt with diagnostics
- `partition_dump.py` host tool to read and pretty-print

**Validation:** HIL tests for: clean A→A boot, clean B→B boot, A bad → fallback to B, both bad → halt.

### Phase 1.8 — OTA over UART (`framing` lib + `ota_send.py`)

**Scope:**
- `lib/framing/`: HDLC-style framing with byte-stuffing, CRC-16, ACK/NACK, sequence numbers
- Bootloader OTA mode entered via magic command from app (writes flag in backup register or RAM, resets)
- Bootloader receives image into inactive slot, verifies, swaps active slot atomically
- `tools/ota_send.py` host driver

**Validation:** End-to-end OTA from PC → device → reboot into new image. HIL automation in CI.

### Phase 1.9 — Anti-rollback

**Scope:**
- Monotonic version counter in slot metadata
- Bootloader refuses to boot a slot whose version < highest-ever-seen (stored in dedicated sector)
- Counter incremented atomically after successful boot

**Validation:** HIL: flash older image → bootloader rejects. Flash newer → boots and counter advances. Re-flash old → still rejected.

### Phase 1.10 — Option byte protection (RDP)

**Scope:**
- Document RDP Level 0/1/2 semantics for STM32F4
- Script to set RDP Level 1 (mass-erase on regression to L0; debug locked)
- HIL test that confirms RDP-1 blocks debug attach
- **Do NOT enable RDP Level 2 on the dev board** — it is permanent. Document the procedure only, leave it as a manual experiment with a separate "burn" board if the user chooses.

**Validation:** HIL confirms debug-attach fails when RDP-1 is set; reverting to L0 mass-erases as expected.

### Phase 1.11 — Threat model & docs

**Scope:**
- `docs/wiki/plans/001-bootloader/threat-model.md`: attacker capabilities, what's protected, what isn't (no anti-glitch, no secure element, software-only key storage)
- `docs/wiki/plans/001-bootloader/production-gap.md`: what would change for production (HSM-backed signing keys, secure-boot ROM, TrustZone-M on M33 parts, anti-tamper, fault injection countermeasures)
- ADR for the chosen image format and partition layout

**Validation:** Doc review; cross-reference from architecture wiki page.

## Risk & rollback

- Bootloader bricking the dev board is the main hazard. Mitigations: keep a known-good bootloader binary; OpenOCD recovery procedure documented in Phase 1.5; never auto-flash bootloader in CI (only the slot images).
- Microeccnees vendoring choice — confirm license + size before committing.
- ECDSA verify time on 100 MHz Cortex-M4 without crypto accelerator is the wildcard. If it exceeds budget, fall back to RSA-PSS-2048 (fixed-time verify with public exponent) or Ed25519. Decision in Phase 1.3.
- RDP Level 2 is irreversible — explicitly out of scope for the dev board.

## Out of scope

- Encrypted firmware images (signed only).
- Anti-tamper, anti-glitch, side-channel-resistant signature verify.
- Secure boot ROM (impossible — F411 has no immutable boot ROM with crypto).
- Wireless OTA (UART only).
