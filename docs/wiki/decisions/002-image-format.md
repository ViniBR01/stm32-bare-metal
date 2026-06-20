# ADR 002: Image Format and Partition Layout

**Date:** 2026-06-19
**Status:** Accepted
**Issues:** #143 (Phase 1.2), #151 (Phase 1.5), #158 (Phase 1.7), #168 (Phase 1.9)

## Context

Plan 001 built the bootloader incrementally across Phases 1.2 through 1.9. Each phase added constraints (signing, A/B fallback, anti-rollback) on top of the on-flash binary format defined in Phase 1.2. Now that all phases have landed and downstream tools (sign_image.py, partition_dump.py, ota_send.py) depend on the exact byte layout, the format is effectively frozen for the STM32F411RE deliverable. This ADR pins the decisions in one place so future contributors know what is load-bearing and why.

## Decisions

### 1. Image header is 140 bytes, packed, CRC-last

`img_header_t` is `__attribute__((packed))` with `_Static_assert(sizeof == 140)`. Fields in order: `magic` (4), `header_version` (4), `image_version` (4), `image_type` (4), `payload_size` (4), `payload_offset` (4), `sha256` (32), `signature` (64), `reserved[4]` (16), `header_crc` (4). Magic is `"IMGH"` = `0x494D4748` little-endian. The trailing CRC covers bytes [0, 136).

### 2. Slot metadata is 36 bytes, packed, CRC-last

`img_slot_metadata_t` fields: `magic` (4), `metadata_version` (4), `active` (4), `fail_count` (4), `monotonic_counter` (4), `reserved[3]` (12), `metadata_crc` (4). Magic is `"SLOT"` = `0x534C4F54` little-endian. The `active` field is `uint32_t` (non-zero = active). CRC covers bytes [0, 32).

### 3. Payload is 512-byte aligned

`payload_offset = 0x200` (512). The 140-byte header is followed by a 372-byte zero-padded gap so the payload's first word (the Cortex-M vector table) lands on a 512-byte boundary. STM32F411 supports up to 96 external IRQs (112 vectors total, 448 bytes), requiring SCB->VTOR alignment to the next power-of-two (512). This alignment holds for all Cortex-M4 parts with up to 96 IRQs.

### 4. Flash sector layout (STM32F411RE, 512 KB)

| Sector | Address | Size | Contents |
|--------|-----------|--------|------------------------------|
| 0 | 0x08000000 | 16 KB | Bootloader (`bootloader_ls.ld`) |
| 1 | 0x08004000 | 16 KB | Slot A metadata (36 B used) |
| 2 | 0x08008000 | 16 KB | Slot B metadata (36 B used) |
| 3 | 0x0800C000 | 16 KB | Reserved (anti-rollback log) |
| 4 | 0x08010000 | 64 KB | Slot A payload (start) |
| 5 | 0x08020000 | 128 KB | Slot A payload (continued) |
| 6 | 0x08040000 | 128 KB | Slot B payload |
| 7 | 0x08060000 | 128 KB | Reserved |

Slot A capacity: 192 KB. Slot B capacity: 128 KB. Bootloader is hard-capped at 16 KB by the linker script; a post-link size guard fails the build if exceeded.

### 5. Signing: ECDSA-P256 over SHA-256, raw R||S

The signature is computed over the SHA-256 digest of the payload (not the header). The 64-byte signature field stores raw `R || S` (32 + 32, big-endian integers). DER encoding is NOT used. The host signer (`sign_image.py`) decodes the DER output from the Python `cryptography` library into raw R||S before packing. The bootloader's `crypto_ecdsa_p256_verify()` expects this raw format directly.

## Consequences

- Changing any field offset or size in `img_header_t` or `img_slot_metadata_t` requires bumping `IMG_HEADER_VERSION` / `IMG_SLOT_METADATA_VERSION`, updating `tools/_img_format.py`, and regenerating the round-trip test fixture. The CI round-trip suite (`tests/tools/sign_roundtrip/`) will fail if the Python and C sides drift.
- Changing the sector layout bricks any device that has already been flashed with the current bootloader, since the bootloader hard-codes metadata and slot addresses.
- The format is frozen for the STM32F411RE deliverable. A different MCU with different sector geometry would need a new partition table (but the header and metadata struct formats can be reused).
- The 16 KB bootloader budget has approximately 4.5 KB of headroom remaining after Phase 1.9.

## Alternatives considered

- **RSA-PSS-2048** — rejected because verify time on a 100 MHz Cortex-M4 without a hardware accelerator exceeds 200 ms, unacceptable for boot latency.
- **Ed25519** — rejected for ecosystem complexity; fewer host-side libraries and tooling compared to ECDSA-P256, and the Python `cryptography` library's Ed25519 API does not expose raw R||S decomposition.
- **DER-encoded signatures** — rejected because a DER parser adds code size to the 16 KB bootloader sector; raw R||S is fixed-length and trivial to index.
- **Single slot (no A/B)** — rejected because OTA updates cannot be made atomic without a fallback slot; a failed flash leaves the device bricked.
- **Metadata embedded in the image header** — rejected because metadata (active flag, fail_count) is written every boot while the image is written only on OTA; separate sectors avoid wearing out the payload sectors.

## References

- [lib/img/inc/img_header.h](../../../lib/img/inc/img_header.h) — struct definitions and static asserts
- [docs/wiki/plans/001-bootloader/image-format.md](../plans/001-bootloader/image-format.md) — Phase 1.2 format spec
- [docs/wiki/plans/001-bootloader/signing.md](../plans/001-bootloader/signing.md) — Phase 1.4 host signing workflow
- [docs/wiki/plans/001-bootloader/ab-slots.md](../plans/001-bootloader/ab-slots.md) — Phase 1.7 A/B partition layout
- [docs/wiki/plans/001-bootloader/verify-and-jump.md](../plans/001-bootloader/verify-and-jump.md) — Phase 1.6 signature verification
- [linker/bootloader_ls.ld](../../../linker/bootloader_ls.ld) — sector 0 linker script
- [linker/app_ls.ld](../../../linker/app_ls.ld) — slot-A app linker script
- [tools/_img_format.py](../../../tools/_img_format.py) — Python format constants (must match C)
- [tools/sign_image.py](../../../tools/sign_image.py) — host signing tool
