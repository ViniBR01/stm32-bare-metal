# Threat Model (Plan 001)

This document captures the security properties of the Plan 001 bootloader,
the attacks each defense addresses, and the explicit gaps the design does
not close.

## Attacker Classes

| # | Class | Capabilities |
|---|---|---|
| 1 | **Network attacker on OTA UART** | Can replay, drop, reorder, or fabricate frames on the physical UART line. Cannot forge an ECDSA-P256 signature without the private key. |
| 2 | **Physical attacker with ST-LINK before RDP-1** | Full flash read/write via OpenOCD/SWD. Can dump images, overwrite metadata sectors, zero the anti-rollback floor, and flash arbitrary binaries into either slot. |
| 3 | **Physical attacker with ST-LINK after RDP-1** | Debug attach succeeds but all user-flash reads return `0xFF`. The only destructive path is regressing to RDP-0 which mass-erases the entire user flash. Result is DoS (blank chip), not compromise. |
| 4 | **Local user with running binary** | Can capture USART2 serial output, invoke CLI commands (including `ota_request`), and observe timing. No debug interface access. |

## Defenses

### Image authenticity

ECDSA-P256 signature over the SHA-256 digest of the payload. The bootloader
rejects any image whose signature does not verify against the embedded public
key.

Sources:
- [`apps/bootloader/loader/verify.c`](../../../../apps/bootloader/loader/verify.c) -- `crypto_ecdsa_p256_verify(bootloader_pubkey, computed, hdr.signature)`
- [`lib/crypto/src/crypto.c`](../../../../lib/crypto/src/crypto.c) -- wraps micro-ecc `uECC_verify` on secp256r1

### Image integrity at rest

The same SHA-256 + ECDSA verification detects flash bit-flips or corruption
in a stored image. A single flipped bit in the payload causes the SHA
comparison to fail; a flipped bit in the header signature causes ECDSA
rejection.

Source: [`apps/bootloader/loader/verify.c`](../../../../apps/bootloader/loader/verify.c) -- SHA mismatch short-circuits before the expensive ECDSA path

### Header integrity

CRC-32 (IEEE 802.3, poly `0xEDB88320`) computed over the first 136 bytes of
the 140-byte `img_header_t`. The parser rejects the header before any
cryptographic verification if the CRC fails.

Source: [`lib/img/inc/img_header.h`](../../../../lib/img/inc/img_header.h) -- `header_crc` field, `img_header_parse()` validation order

### Atomic slot swap

Two-step metadata commit after a successful OTA verify. Power-cut between
step 1 and step 2 leaves both slots marked `active=1`; the decision tree
resolves this by picking the higher `monotonic_counter` (always the
freshly-written slot). A power-cut inside a metadata sector erase leaves
all-`0xFF`, which fails CRC and is treated as "invalid slot" -- falls back
to the other.

Source: [ota.md](ota.md) -- "Active-slot swap (mid-OTA power-cut safety)"

### Anti-rollback

The bootloader computes a floor as `max(slot_a.monotonic_counter,
slot_b.monotonic_counter)` across all valid metadata sectors. Any image
whose `image_version < floor` is rejected even if correctly signed. The
floor advances monotonically with each successful boot.

Source: [anti-rollback.md](anti-rollback.md) -- `img_header_meets_floor`, `img_compute_floor`, `img_compute_new_floor`

### Readback protection

RDP Level 1 blocks the debug interface from reading user flash. An attacker
who attaches ST-LINK sees `0xFF` for every word. Regressing to RDP-0
mass-erases the chip, yielding a blank board rather than a readable one.

Source: [rdp.md](rdp.md) -- option-byte `FLASH_OPTCR.RDP[7:0]` set to `0xBB`

### OTA wire integrity

CRC-16-CCITT (poly `0x1021`, init `0xFFFF`) computed per frame over the
unstuffed header + payload span. Bad-CRC frames are silently dropped. The
sliding-window-1 ARQ retransmits on host timeout, and duplicate SEQs are
idempotently re-ACKed without re-executing the write.

Source: [`lib/framing/inc/framing.h`](../../../../lib/framing/inc/framing.h) -- `frame_crc16`, `frame_link_t` ARQ state machine

### Fail-count rollback-on-crash

The bootloader increments `fail_count` in metadata before jumping to an app.
The app clears it post-init via `bl_handshake_clear_fail_count()`. Three
consecutive crashes without a clear mark the slot dead and trigger fallback
to the other slot.

Source: [anti-rollback.md](anti-rollback.md) -- "fail_count semantics"

## Explicit Non-Goals

The following attacks are **not** mitigated by Plan 001. Each is a conscious
design decision, not an oversight.

1. **Image confidentiality.** Images are signed but not encrypted. An
   attacker who obtains a `.signed.bin` (e.g. from the OTA UART line before
   RDP-1) can reverse-engineer the firmware. Plan 001 scope excludes
   encrypted payloads.

2. **Side-channel attacks on ECDSA.** `crypto_memcmp_ct` in
   [`lib/crypto/src/crypto.c`](../../../../lib/crypto/src/crypto.c) is
   constant-time, but the micro-ecc P-256 curve operations are **not**.
   An attacker with a power/EM probe can potentially recover intermediate
   scalar values regardless of RDP level.

3. **Glitch attacks.** There is no redundant verify pass, no double-check of
   the jump target, and no voltage/clock glitch detection. A well-timed
   fault injection could skip the signature check entirely.

4. **Replay/downgrade pre-RDP-1.** With OpenOCD access an attacker can wipe
   metadata sectors (zeroing the floor) and flash any old signed image.
   RDP-1 is the defense; without it, anti-rollback protects only against
   OTA-path downgrades.

5. **Backup-register secrets.** `RTC_BKP_DR0..DR19` are **not** protected by
   RDP. Only the non-secret OTA magic (`0x4F544131`) is stored there. No
   keys or session material may be placed in backup registers.
   Source: [rdp.md](rdp.md) -- "RTC_BKP not protected by RDP"

6. **DoS via OTA path.** The `ota_request` CLI command is unauthenticated.
   Any local user (attacker class 4) can force the chip into OTA mode. The
   active slot is never overwritten during OTA (target must be the inactive
   slot), so the worst outcome is a failed OTA attempt after which the chip
   reboots into its existing active image.

## Summary Table

| Attacker | Attack | Defense | Residual Gap |
|---|---|---|---|
| 1 (network) | Fabricate a malicious OTA image | ECDSA-P256 signature verification ([verify.c](../../../../apps/bootloader/loader/verify.c)) | None -- forging requires the private key |
| 1 (network) | Replay an older signed image over OTA | Anti-rollback floor check ([anti-rollback.md](anti-rollback.md)) | None -- `image_version < floor` is rejected |
| 1 (network) | Corrupt in-flight OTA data | CRC-16-CCITT per frame + ARQ retransmit ([lib/framing/](../../../../lib/framing/inc/framing.h)) | None -- corrupted frames are dropped and retried; corrupted images fail SHA check |
| 1 (network) | DoS the OTA receiver (garbage frames) | Active slot untouched; chip reboots into last-good image | Bootloader is stuck in OTA mode until timeout or reset; no permanent harm |
| 2 (physical, pre-RDP) | Read flash to extract firmware | **Unmitigated** -- RDP-0 allows full readback | Close with RDP-1 ([rdp.md](rdp.md)) |
| 2 (physical, pre-RDP) | Wipe rollback floor and flash old image | **Unmitigated** -- OpenOCD can `mww` metadata sectors | Close with RDP-1; same attacker already has full flash write |
| 2 (physical, pre-RDP) | Replace public key in sector 0 | **Unmitigated** -- full flash write access | Close with RDP-1; post-RDP-1, sector 0 is read-protected |
| 3 (physical, post-RDP) | Read flash contents | RDP-1 returns `0xFF` on debug reads ([rdp.md](rdp.md)) | None -- only mass-erase (L1 to L0) regains access |
| 3 (physical, post-RDP) | Mass-erase to reset chip | Mass-erase wipes all user flash including bootloader | DoS only -- attacker gets a blank chip, not a downgraded one |
| 3 (physical, post-RDP) | Glitch past signature verify | **Unmitigated** -- no redundant verify or glitch detection | Out of scope for F411 dev-board threat model |
| 4 (local user) | Trigger OTA to install old image via replay | Anti-rollback floor rejects `image_version < floor` | None |
| 4 (local user) | Force OTA mode to disrupt service | Active slot survives; chip reboots normally after failed OTA | Temporary service disruption (DoS) until next reboot |
| 4 (local user) | Timing side-channel on ECDSA verify | **Unmitigated** -- micro-ecc is not constant-time | Out of scope; see Explicit Non-Goals |

## Trust Assumptions

The security of Plan 001 rests on these assumptions holding true:

1. **Signing key never leaves the host.** The ECDSA-P256 private key used by
   `tools/sign_image.py` is never embedded in firmware or transmitted over
   any link. If compromised, all defenses against fabricated images collapse.

2. **Public key in bootloader sector is authentic.** The 64-byte
   `bootloader_pubkey[]` linked into sector 0 is the correct counterpart of
   the signing key. If an attacker substitutes a different public key (only
   possible pre-RDP-1 via OpenOCD), they can sign images with the
   corresponding private key.

3. **Bootloader sector integrity.** Sector 0 is not writable by any code
   path after RDP-1 is engaged. The bootloader never self-modifies. Under
   RDP-1, an attacker's only option is mass-erase (which also wipes the
   bootloader, yielding DoS not compromise).

4. **Flash controller operates correctly.** The STM32F4 flash controller
   performs writes and erases as documented. Wear-out, stuck bits, or
   controller bugs are not accounted for beyond the CRC-32 header check and
   the SHA-256 payload check that detect post-write corruption.

5. **Clock and voltage within spec.** The bootloader assumes SYSCLK is
   100 MHz (for DWT cycle counting) and that V_DD stays within the
   datasheet-specified range. Out-of-spec conditions could cause
   unpredictable behaviour in the flash controller or CPU.
