# Plan 001 — Image Header & Slot Metadata Format

**Phase:** 1.2
**Status:** landed (via Issue [#143](https://github.com/ViniBR01/stm32-bare-metal/issues/143))
**Implementation:** `lib/img/`
**Tests:** `tests/lib/img/`

## Purpose

Defines the on-flash binary layout that the host signing tool writes and the
bootloader reads, and the slot-metadata layout the A/B fallback logic uses.
Phase 1.2 lands the format and a host-validated parser only — the SHA-256 and
ECDSA signature fields are populated with placeholder bytes here and become
authoritative in Phase 1.3 (`#144`, crypto primitives).

The format is deliberately minimal:

- Pure C (`<stdint.h>`, `<stddef.h>`, `<string.h>`) so the same parser
  compiles for host gcc and `arm-none-eabi-gcc`.
- Software-only CRC-32 (no STM32 hardware CRC engine), so the host signer
  matches without special hardware.
- `__attribute__((packed))` on the structs and `_Static_assert` on their
  `sizeof` — any silent layout change breaks the build on both host and
  target.

## Image header — `img_header_t`

Total size: **140 bytes**. Lives at the start of every signed image.

| Offset | Size | Field             | Description                                                       |
|--------|------|-------------------|-------------------------------------------------------------------|
| 0      | 4    | `magic`           | `0x494D4748` — ASCII `"IMGH"` (little-endian).                    |
| 4      | 4    | `header_version`  | Header format version. Currently `1`.                             |
| 8      | 4    | `image_version`   | Monotonic firmware version. Used by Phase 1.9 anti-rollback.      |
| 12     | 4    | `image_type`      | `1 = IMG_TYPE_BOOTLOADER`, `2 = IMG_TYPE_APP`.                    |
| 16     | 4    | `payload_size`    | Bytes of payload after the header.                                |
| 20     | 4    | `payload_offset`  | Bytes from header start to payload start. Must be ≥ 140.          |
| 24     | 32   | `sha256`          | SHA-256 of payload. Placeholder until Phase 1.3.                  |
| 56     | 64   | `signature`       | ECDSA P-256 signature, raw `R \|\| S`. Placeholder until Phase 1.3. |
| 120    | 16   | `reserved[4]`     | Reserved for future use. Parser accepts any value.                |
| 136    | 4    | `header_crc`      | CRC-32 over bytes `[0, 136)`.                                     |

## Slot metadata — `img_slot_metadata_t`

Total size: **36 bytes**. Lives in a dedicated metadata sector per slot, **not**
embedded in the image. Phase 1.7 uses it for A/B fallback; Phase 1.9 uses
`monotonic_counter` for anti-rollback.

| Offset | Size | Field                | Description                                                    |
|--------|------|----------------------|----------------------------------------------------------------|
| 0      | 4    | `magic`              | `0x534C4F54` — ASCII `"SLOT"` (little-endian).                 |
| 4      | 4    | `metadata_version`   | Currently `1`.                                                 |
| 8      | 4    | `active`             | Non-zero = active slot.                                        |
| 12     | 4    | `fail_count`         | Bootloader increments before jump; app clears after init.      |
| 16     | 4    | `monotonic_counter`  | Anti-rollback floor (Phase 1.9).                               |
| 20     | 12   | `reserved[3]`        | Reserved for future use. Parser accepts any value.             |
| 32     | 4    | `metadata_crc`       | CRC-32 over bytes `[0, 32)`.                                   |

## CRC-32 algorithm

Standard IEEE 802.3 / zlib CRC-32, software-only. Spec sufficient for an
independent (e.g. Python) signer to match byte-for-byte:

| Property      | Value                |
|---------------|----------------------|
| Polynomial    | `0xEDB88320` (reflected of `0x04C11DB7`) |
| Initial value | `0xFFFFFFFF`         |
| Reflect input | yes (LSB-first)      |
| Reflect output| yes                  |
| Final XOR     | `0xFFFFFFFF`         |

Reference vector: `CRC32("123456789") == 0xCBF43926`. Implementation in
`lib/img/src/img_header.c` is bytewise (no precomputed table) — header and
metadata CRCs cover ≤ 140 bytes, so the table cost would not pay back the
bootloader-sector flash budget.

The `header_crc` / `metadata_crc` fields cover every preceding byte in the
struct, in raw flash order.

## Magic values

| Constant                   | Hex value     | ASCII   |
|----------------------------|---------------|---------|
| `IMG_HEADER_MAGIC`         | `0x494D4748`  | `IMGH`  |
| `IMG_SLOT_METADATA_MAGIC`  | `0x534C4F54`  | `SLOT`  |

Both are stored little-endian, so the byte sequence on flash is `48 47 4D 49`
("HGMI" if you read raw bytes left-to-right) for `IMGH`, etc.

## Parser API

```c
img_err_t img_header_parse(const uint8_t *buf, size_t buf_len, img_header_t *out);
img_err_t img_slot_metadata_parse(const uint8_t *buf, size_t buf_len,
                                  img_slot_metadata_t *out);
uint32_t  img_crc32(const uint8_t *buf, size_t len);
```

### Validation order (first failure wins)

For `img_header_parse`:

1. NULL `buf` or `out` → `IMG_ERR_NULL_ARG`.
2. `buf_len < sizeof(img_header_t)` → `IMG_ERR_BAD_SIZE`.
3. Trailing CRC does not match recomputed CRC over preceding bytes → `IMG_ERR_BAD_CRC`.
4. `magic != IMG_HEADER_MAGIC` → `IMG_ERR_BAD_MAGIC`.
5. `header_version != 1` → `IMG_ERR_BAD_VERSION`.
6. `image_type` not in `{1, 2}` → `IMG_ERR_BAD_TYPE`.
7. `payload_offset < sizeof(img_header_t)` → `IMG_ERR_BAD_OFFSET`.
8. `payload_size == 0` → `IMG_ERR_BAD_SIZE`.

For `img_slot_metadata_parse`:

1. NULL args → `IMG_ERR_NULL_ARG`.
2. Buffer too small → `IMG_ERR_BAD_SIZE`.
3. CRC mismatch → `IMG_ERR_BAD_CRC`.
4. Bad magic → `IMG_ERR_BAD_MAGIC`.
5. `metadata_version != 1` → `IMG_ERR_BAD_VERSION`.

CRC is checked before magic because a corrupted-flash header that *happens* to
look like a valid magic must not be parsed; the integrity check has to gate
field interpretation. Tests `test_header_bad_magic` /
`test_metadata_bad_magic` recompute the CRC after tampering specifically to
exercise the magic check.

### Error codes

| Code                  | Value | Meaning                                                  |
|-----------------------|-------|----------------------------------------------------------|
| `IMG_OK`              |  0    | Parsed and validated.                                    |
| `IMG_ERR_BAD_MAGIC`   | -1    | Magic field does not match expected value.               |
| `IMG_ERR_BAD_VERSION` | -2    | Header / metadata version is not the supported value.    |
| `IMG_ERR_BAD_TYPE`    | -3    | `image_type` is not a known enum.                        |
| `IMG_ERR_BAD_SIZE`    | -4    | Buffer too small or payload size invalid.                |
| `IMG_ERR_BAD_OFFSET`  | -5    | Payload offset overlaps the header.                      |
| `IMG_ERR_BAD_CRC`     | -6    | CRC over preceding bytes does not match trailing field.  |
| `IMG_ERR_NULL_ARG`    | -7    | A required pointer is NULL.                              |

## Future work

- Phase 1.3 (#144): replace placeholder SHA-256 / signature bytes with real
  values produced by `lib/crypto/`.
- Phase 1.4: host signer (`tools/sign_image.py`) writes this exact format,
  cross-validated against the CRC-32 reference vector and against the parser
  here.
- Phase 1.7: slot metadata becomes load-bearing for A/B fallback. The
  `reserved[3]` words are an intentional cushion for that phase.
- Phase 1.9: anti-rollback uses `image_version` (image header) and
  `monotonic_counter` (slot metadata).
