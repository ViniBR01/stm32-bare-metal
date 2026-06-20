# Production Gaps (Plan 001)

What this project would need to change to ship as a real product, ordered
roughly by cost and effort. Each gap names a specific replacement technology
so a reader who wants to close it knows where to start.

## Summary

| # | Gap | Current State | Production Requirement | Effort |
|---|-----|---------------|------------------------|--------|
| 1 | Signing-key custody | Fixed seed, regenerated every build | HSM-backed key + signing server | Low (infra only) |
| 2 | Secure boot ROM | Bootloader in writable sector 0 | Immutable ROM with crypto (STM32H7/U5) | High (MCU change) |
| 3 | TrustZone-M | Single privilege level | Secure/non-secure world split | High (MCU change) |
| 4 | Anti-glitch / anti-tamper | No countermeasures | Redundant verify, tamper pins, glitch detection | Medium–High |
| 5 | Encrypted firmware images | Signed but readable | Per-device AES key from UID-keyed KDF | Medium |
| 6 | Side-channel resistance | micro-ecc best-effort | Certified SCA-resistant library or HW accelerator | Medium |
| 7 | Secure provisioning | No per-device secrets | Factory attestation + key injection | Medium (process) |
| 8 | Audit trail | Reproducible but not auditable | Tamper-evident signing log | Low (backend) |

---

## 1. Signing-Key Custody

**Current state:**
[`tools/keygen.py`](../../../../tools/keygen.py) derives the ECDSA-P256 keypair from a
hard-coded seed string (`stm32-bare-metal-dev-fixture`). `make keys` regenerates the
keypair at the start of every build so CI is deterministic and reproducible.

**Production requirement:**
The private key lives in a Hardware Security Module (HSM) or cloud KMS. A signing server
accepts build artifacts, queues them for human review, signs with the HSM-held key, and
returns the `.signed.bin`. The private key never touches a developer workstation or CI
runner disk.

**What doesn't change:**
The device-side public key embedding ([`build/keys/bootloader_pubkey.c`](../../../../apps/bootloader/loader/verify.c))
is identical — it's just a 64-byte constant linked into sector 0. Only the key's provenance
changes.

**Effort:** Low — no firmware changes required. Purely infrastructure (HSM provisioning,
signing-server deployment, access-control policy).

---

## 2. Secure Boot ROM

**Current state:**
The bootloader lives in writable sector 0 (16 KB). It is the root of trust. If an
attacker glitches or interrupts sector-0 reflash (followed by a power cycle), the
STM32F411's built-in system memory bootloader (UART/USB DFU at `0x1FFF0000`) takes over —
with no signature checking whatsoever.

**Production requirement:**
An MCU with an **immutable boot ROM** containing cryptographic verification. The ROM
verifies the first user-code block (our bootloader) before executing it, forming a
hardware-rooted chain of trust that cannot be bypassed by flash manipulation.

**What closes it:**
- **STM32H7** series: Secure Boot with RSA/ECDSA verification in the ROM.
- **STM32U5/L5** series: TrustZone-M + Secure Boot Manager (SBSFU reference).
- **STM32H5** series: Immutable root-of-trust ROM + secure provisioning.

**This is the single biggest gap.** Without an immutable ROM, the entire security model
collapses if an attacker can write to sector 0 (pre-RDP-1) or glitch the boot sequence.

**Effort:** High — requires an MCU change (~$3–8 BOM delta depending on part), board
redesign, and 4–8 weeks of integration to port drivers + bootloader to the new part.

---

## 3. TrustZone-M

**Current state:**
All code (bootloader, app, drivers) runs at the same privilege level in a single
execution environment. The verify path, floor counter, and slot-pick decision are
protected only by sector-0's RDP-1 read protection.

**Production requirement:**
Split execution into secure and non-secure worlds:
- **Secure world:** signature verification, anti-rollback floor read/write, active-slot
  decision, public key storage, metadata commit.
- **Non-secure world:** application code, OTA framing layer, CLI, peripheral drivers.

The non-secure world cannot call into secure functions except through a well-defined
Non-Secure Callable (NSC) API, preventing a compromised app from bypassing the bootloader's
security checks at runtime.

**What closes it:**
STM32U5 or STM32L5 with ARMv8-M TrustZone. ST's SBSFU (Secure Boot and Secure Firmware
Update) reference implementation demonstrates the pattern.

**Effort:** High — MCU change + significant firmware restructuring. The entire verify path
and metadata handling must move behind the secure/non-secure boundary. Estimated 6+ weeks.

---

## 4. Anti-Glitch and Anti-Tamper

**Current state:**
No countermeasures. The STM32F411's PVD (Programmable Voltage Detector) can detect
brown-outs but is far too coarse (~100 mV hysteresis, ~µs response) to catch a sub-
microsecond voltage glitch targeting a specific instruction.

**Production requirement:**
- Redundant verify: execute the SHA + ECDSA check twice and compare results.
- Dual-execution: run critical branch decisions through two independent code paths;
  halt if they disagree.
- Voltage-glitch detection: dedicated analog front-end or silicon-integrated glitch
  detector (e.g. STM32H5 TAMP pins with active shield).
- Mesh shielding / decap detection: top-metal shield with integrity monitoring.

**What STM32F411 offers:** Almost nothing. PVD with interrupt, watchdog (IWDG/WWDG).
No tamper pins, no active shield, no glitch detector.

**What closes it (silicon level):**
- STM32H5: active tamper pins, internal voltage/clock monitors, secure debug authentication.
- External: dedicated security co-processor (e.g. ATECC608B, OPTIGA Trust M).

**Effort:** Medium–High. Software-only mitigations (redundant verify, dual-execution) add
~1–2 KB to sector 0 and ~20% boot-time overhead. Full hardware protection requires MCU/board
change.

---

## 5. Encrypted Firmware Images

**Current state:**
Images are signed but transmitted and stored in plaintext. A physical attacker (pre-RDP-1)
or a passive eavesdropper on the OTA UART can obtain the full firmware binary for
reverse-engineering.

**Production requirement:**
Encrypt the image payload with a per-device symmetric key so only the target device can
decrypt. Typical approach: AES-128-CTR with a key derived from the device's unique ID
(`STM32_UID`) via a KDF (e.g. HKDF-SHA256) seeded with a provisioning secret.

**What it requires:**
- Per-device factory provisioning step to inject the KDF seed.
- Secure storage for the seed on-chip (OTP fuses or RDP-1-protected flash region).
- Host-side: device registry mapping UID → encryption key for build-time image encryption.
- Firmware: decrypt-in-place before verify (adds ~2 KB code + ~50 ms boot latency for
  AES-128-CTR of a 128 KB image on Cortex-M4 at 100 MHz).

**Effort:** Medium — firmware changes are moderate, but the per-device provisioning
infrastructure is the expensive part.

---

## 6. Side-Channel Resistance

**Current state:**
[`lib/crypto/src/crypto.c`](../../../../lib/crypto/src/crypto.c) wraps the vendored micro-ecc
library. `crypto_memcmp_ct()` is constant-time, but the P-256 scalar multiplication and
point operations in micro-ecc are **not** designed for side-channel resistance. Timing,
power, and EM leakage patterns vary with the scalar (private key not at risk since we only
verify; but the verification result itself could be influenced by a combined
fault+side-channel attack).

**Production requirement:**
A certified constant-time ECDSA implementation with documented SCA countermeasures:
- **mbedTLS** with `MBEDTLS_ECP_INTERNAL_ALT` and blinding enabled (~15 KB code).
- **Hardware accelerator:** STM32 parts with PKA (Public Key Accelerator) — available on
  STM32U5, STM32WB, STM32L4+. The F411 has no crypto accelerator.

**Effort:** Medium — mbedTLS integration is well-documented but adds significant code size
(likely exceeds the 16 KB sector-0 budget; would require moving verify to a separate flash
region or choosing an MCU with more boot ROM space).

---

## 7. Secure Provisioning Flow

**Current state:**
No per-device secrets. Every device built from the same source tree has the same public
key. The [`tools/keygen.py`](../../../../tools/keygen.py) fixed seed means any developer
with the source can regenerate the "production" key pair.

**Production requirement:**
Factory provisioning under attestation:
1. Generate a unique device identity (certificate or key pair) in a secure environment.
2. Inject the signing public key + per-device secrets (encryption key seed, device cert)
   into OTP or protected flash.
3. Record the provisioning event in an attestation log (device ID, timestamp, operator,
   firmware version provisioned).
4. Lock down the provisioning interface (disable JTAG/SWD after provisioning, or use
   secure debug authentication on STM32H5).

**Effort:** Medium — primarily process and tooling. Firmware changes are small (read
provisioned key from OTP instead of linked constant). The factory-floor infrastructure
(provisioning jig, attestation backend, device registry) is the bulk of the work.

---

## 8. Audit Trail

**Current state:**
`make keys` regenerates the keypair deterministically from a fixed seed. Builds are
reproducible: the same source tree always produces the same `.signed.bin`. But there is
no record of *who* signed *what* *when*, or whether a given signed image was authorized.

**Production requirement:**
- Every signing operation logged with: timestamp, operator identity, image hash,
  image version, signing key fingerprint.
- Every OTA push logged with: target device ID, image version, push result.
- Every key rotation event logged with: old key fingerprint, new key fingerprint,
  authorizing operator(s).
- Tamper-evident log storage (append-only, cryptographically chained).

**What closes it:**
A signing-server backend (e.g. AWS CloudHSM + CloudTrail, or Sigstore/Rekor for
open-source transparency) combined with a device management platform that records
OTA delivery events.

**Effort:** Low for the embedded side (no firmware changes). Medium for the backend
infrastructure. Completely out of scope for a hobby/learning project; mentioned for
completeness.

---

## Cross-Reference to Industry Standards

| Gap | Relevant Standard / Framework |
|-----|-------------------------------|
| Signing-key custody | NIST SP 800-57 (Key Management), FIPS 140-3 (HSM certification) |
| Secure boot ROM | ARM PSA Certified Level 2, GlobalPlatform TEE |
| TrustZone-M | ARM PSA Certified, SESIP (Security Evaluation Standard for IoT Platforms) |
| Anti-glitch | Common Criteria (CC) AVA_VAN.5, EMVCo security evaluation |
| Encrypted images | NIST SP 800-175B (crypto for firmware protection) |
| Side-channel | ISO/IEC 17825 (SCA testing), FIPS 140-3 Level 3+ |
| Provisioning | NIST SP 800-193 (Platform Firmware Resilience), TCG DICE |
| Audit trail | NIST SP 800-92 (Log Management), RFC 9162 (Certificate Transparency) |
