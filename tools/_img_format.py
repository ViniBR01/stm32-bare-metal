"""Shared on-flash image format spec for Plan 001 host tooling.

Single Python source of truth for the byte layouts defined in
lib/img/inc/img_header.h. Used by tools/keygen.py, tools/sign_image.py, and
any future tool (e.g. partition_dump.py in Phase 1.7).

Drift between this module and the C side is a load-bearing bug — every CI
build runs a round-trip test under tests/tools/sign_roundtrip/ that signs a
fixture with the Python tools and parses it with the real C parser, so any
mismatch fails host-tests immediately.

Dependencies: stdlib only.
"""

import struct

# Magic constants (little-endian uint32s when written to flash).
IMG_HEADER_MAGIC = 0x494D4748  # "IMGH"
IMG_SLOT_METADATA_MAGIC = 0x534C4F54  # "SLOT"

# Format versions.
IMG_HEADER_VERSION = 1
IMG_SLOT_METADATA_VERSION = 1

# Image type enum values (uint32_t in the header).
IMG_TYPE_BOOTLOADER = 1
IMG_TYPE_APP = 2

# Field sizes.
IMG_SHA256_SIZE = 32
IMG_SIGNATURE_SIZE = 64  # ECDSA P-256 raw R||S
IMG_HEADER_SIZE = 140
IMG_SLOT_METADATA_SIZE = 36

# Default payload_offset used by sign_image.py.  The payload's first word
# is the app vector table, which Cortex-M4 SCB->VTOR requires aligned to a
# power-of-two >= the vector table size.  STM32F4 has 96 IRQs (vectors
# 0..111), so the table is up to 448 bytes, requiring 512-byte alignment.
# The 140-byte header is followed by 372 bytes of zero padding so the
# payload's first word lands at a 512-byte boundary.
IMG_PAYLOAD_OFFSET_DEFAULT = 512

# Pubkey size mirrors CRYPTO_ECDSA_P256_PUBKEY_LEN in lib/crypto/inc/crypto.h.
CRYPTO_ECDSA_P256_PUBKEY_LEN = 64

# struct format strings — little-endian, no padding (matches __attribute__((packed))).
# Header: 6 uint32 + 32-byte sha256 + 64-byte signature + 4 reserved uint32 + crc.
# We pack the leading 136 bytes here; the trailing 4-byte CRC is appended after
# computation to keep the format string aligned with the "preceding bytes" the
# C parser CRCs.
_HEADER_PRECRC_FMT = "<6I32s64s4I"
_HEADER_PRECRC_SIZE = struct.calcsize(_HEADER_PRECRC_FMT)
assert _HEADER_PRECRC_SIZE == IMG_HEADER_SIZE - 4, (
    f"header pre-CRC size {_HEADER_PRECRC_SIZE} != expected {IMG_HEADER_SIZE - 4}"
)

_SLOT_PRECRC_FMT = "<5I3I"
_SLOT_PRECRC_SIZE = struct.calcsize(_SLOT_PRECRC_FMT)
assert _SLOT_PRECRC_SIZE == IMG_SLOT_METADATA_SIZE - 4, (
    f"slot pre-CRC size {_SLOT_PRECRC_SIZE} != expected {IMG_SLOT_METADATA_SIZE - 4}"
)


def crc32(buf: bytes) -> int:
    """CRC-32 matching lib/img/src/img_header.c::img_crc32 byte-for-byte.

    IEEE 802.3 / zlib: polynomial 0xEDB88320, init 0xFFFFFFFF, reflected,
    final XOR 0xFFFFFFFF. Bytewise table-free — same loop as the C side.
    """
    crc = 0xFFFFFFFF
    for byte in buf:
        crc ^= byte
        for _ in range(8):
            mask = 0xFFFFFFFF if (crc & 1) else 0
            crc = (crc >> 1) ^ (0xEDB88320 & mask)
    return crc ^ 0xFFFFFFFF


def pack_header(
    image_version: int,
    image_type: int,
    payload_size: int,
    sha256: bytes,
    signature: bytes,
    *,
    payload_offset: int = IMG_HEADER_SIZE,
    reserved: tuple[int, int, int, int] = (0, 0, 0, 0),
) -> bytes:
    """Pack a 140-byte img_header_t with a valid trailing CRC.

    Caller supplies the SHA-256 digest of the payload and the 64-byte raw
    R||S ECDSA-P256 signature over that digest. Magic, header_version, and
    payload_offset are filled in automatically.
    """
    if len(sha256) != IMG_SHA256_SIZE:
        raise ValueError(f"sha256 must be {IMG_SHA256_SIZE} bytes, got {len(sha256)}")
    if len(signature) != IMG_SIGNATURE_SIZE:
        raise ValueError(
            f"signature must be {IMG_SIGNATURE_SIZE} bytes, got {len(signature)}"
        )
    if image_type not in (IMG_TYPE_BOOTLOADER, IMG_TYPE_APP):
        raise ValueError(f"unknown image_type {image_type}")
    if payload_size <= 0:
        raise ValueError(f"payload_size must be > 0, got {payload_size}")
    if payload_offset < IMG_HEADER_SIZE:
        raise ValueError(
            f"payload_offset must be >= {IMG_HEADER_SIZE}, got {payload_offset}"
        )

    pre_crc = struct.pack(
        _HEADER_PRECRC_FMT,
        IMG_HEADER_MAGIC,
        IMG_HEADER_VERSION,
        image_version,
        image_type,
        payload_size,
        payload_offset,
        sha256,
        signature,
        *reserved,
    )
    crc = crc32(pre_crc)
    return pre_crc + struct.pack("<I", crc)


def pack_slot_metadata(
    *,
    active: int,
    fail_count: int,
    monotonic_counter: int,
    reserved: tuple[int, int, int] = (0, 0, 0),
) -> bytes:
    """Pack a 36-byte img_slot_metadata_t with a valid trailing CRC."""
    pre_crc = struct.pack(
        _SLOT_PRECRC_FMT,
        IMG_SLOT_METADATA_MAGIC,
        IMG_SLOT_METADATA_VERSION,
        active,
        fail_count,
        monotonic_counter,
        *reserved,
    )
    crc = crc32(pre_crc)
    return pre_crc + struct.pack("<I", crc)


def _selftest() -> int:
    """Lightweight cross-check against published reference values.

    Returns process exit code: 0 on pass, 1 on fail.
    """
    failures = 0

    # Reference vector matched by tests/lib/img/test_img_header.c.
    ref = crc32(b"123456789")
    if ref != 0xCBF43926:
        print(f"FAIL: crc32('123456789') = 0x{ref:08x}, expected 0xCBF43926")
        failures += 1
    else:
        print(f"OK   crc32('123456789') = 0x{ref:08x}")

    # Empty buffer: init XOR final XOR cancel.
    empty = crc32(b"")
    if empty != 0:
        print(f"FAIL: crc32(b'') = 0x{empty:08x}, expected 0x00000000")
        failures += 1
    else:
        print(f"OK   crc32(b'')        = 0x{empty:08x}")

    # Size pinning.
    fake_sha = b"\x00" * IMG_SHA256_SIZE
    fake_sig = b"\x00" * IMG_SIGNATURE_SIZE
    blob = pack_header(
        image_version=1,
        image_type=IMG_TYPE_APP,
        payload_size=1,
        sha256=fake_sha,
        signature=fake_sig,
    )
    if len(blob) != IMG_HEADER_SIZE:
        print(f"FAIL: pack_header() len {len(blob)}, expected {IMG_HEADER_SIZE}")
        failures += 1
    else:
        print(f"OK   pack_header() len = {len(blob)}")

    md = pack_slot_metadata(active=1, fail_count=0, monotonic_counter=0)
    if len(md) != IMG_SLOT_METADATA_SIZE:
        print(
            f"FAIL: pack_slot_metadata() len {len(md)}, expected {IMG_SLOT_METADATA_SIZE}"
        )
        failures += 1
    else:
        print(f"OK   pack_slot_metadata() len = {len(md)}")

    return 1 if failures else 0


if __name__ == "__main__":
    import sys

    sys.exit(_selftest())
