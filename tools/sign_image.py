#!/usr/bin/env python3
"""Sign a firmware payload and emit a flashable .signed.bin.

Reads a raw `.bin` payload, computes its SHA-256, signs that digest with an
ECDSA P-256 private key, and prepends the 140-byte `img_header_t` defined by
lib/img/inc/img_header.h. The header layout, magic constants, and CRC are
shared with the C parser via tools/_img_format.py — the round-trip test at
tests/tools/sign_roundtrip/ asserts byte-level compatibility on every CI run.

Output layout:
  [ 140-byte img_header_t ] [ payload bytes ]

Usage:
  python3 tools/sign_image.py \\
      --priv-in <key>.pem \\
      --in <payload>.bin \\
      --out <name>.signed.bin \\
      --image-type {bootloader,app} \\
      --image-version <int>

Dependencies: cryptography (`pip install cryptography`).
"""

import argparse
import hashlib
import sys
from pathlib import Path

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature

# Allow running as a script (python3 tools/sign_image.py) and as a module.
sys.path.insert(0, str(Path(__file__).resolve().parent))
import _img_format as fmt  # noqa: E402

IMAGE_TYPE_NAMES = {
    "bootloader": fmt.IMG_TYPE_BOOTLOADER,
    "app": fmt.IMG_TYPE_APP,
}


def sign_payload(priv_key: ec.EllipticCurvePrivateKey, payload: bytes) -> tuple[bytes, bytes]:
    """Return (sha256_digest, raw_R_S_64_bytes).

    The digest is the SHA-256 of the payload. The signature is ECDSA-P256
    over that digest (cryptography hashes internally with SHA-256), then
    decoded from DER into raw R||S big-endian 32+32 = 64 bytes — the format
    crypto_ecdsa_p256_verify expects.
    """
    digest = hashlib.sha256(payload).digest()
    sig_der = priv_key.sign(payload, ec.ECDSA(hashes.SHA256()))
    r, s = decode_dss_signature(sig_der)
    sig_raw = r.to_bytes(32, "big") + s.to_bytes(32, "big")
    return digest, sig_raw


def build_signed_image(
    priv_key: ec.EllipticCurvePrivateKey,
    payload: bytes,
    *,
    image_type: int,
    image_version: int,
    payload_offset: int = fmt.IMG_PAYLOAD_OFFSET_DEFAULT,
) -> bytes:
    """Return a complete signed image: header || padding || payload.

    The default payload_offset is 256 so the payload's first word — which
    is the app vector table — lands at a 128-byte boundary suitable for
    the Cortex-M4 SCB->VTOR alignment requirement.  Bytes between the
    140-byte header and the payload are zero-filled.
    """
    if not payload:
        raise ValueError("payload is empty")
    if payload_offset < fmt.IMG_HEADER_SIZE:
        raise ValueError(
            f"payload_offset {payload_offset} < header size {fmt.IMG_HEADER_SIZE}"
        )

    digest, signature = sign_payload(priv_key, payload)
    header = fmt.pack_header(
        image_version=image_version,
        image_type=image_type,
        payload_size=len(payload),
        sha256=digest,
        signature=signature,
        payload_offset=payload_offset,
    )
    pad = b"\x00" * (payload_offset - fmt.IMG_HEADER_SIZE)
    return header + pad + payload


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Sign a firmware payload and emit a flashable .signed.bin."
    )
    parser.add_argument(
        "--priv-in",
        required=True,
        type=Path,
        help="PEM-encoded ECDSA-P256 private key (PKCS#8, unencrypted).",
    )
    parser.add_argument(
        "--in",
        dest="payload_in",
        required=True,
        type=Path,
        help="Raw firmware payload (.bin).",
    )
    parser.add_argument(
        "--out",
        required=True,
        type=Path,
        help="Output path for the signed image (.signed.bin).",
    )
    parser.add_argument(
        "--image-type",
        required=True,
        choices=sorted(IMAGE_TYPE_NAMES.keys()),
        help="Image type — must match the slot the bootloader will load it into.",
    )
    parser.add_argument(
        "--image-version",
        required=True,
        type=int,
        help="Monotonic firmware version (Phase 1.9 anti-rollback uses this).",
    )
    args = parser.parse_args(argv)

    if args.image_version < 0 or args.image_version > 0xFFFFFFFF:
        parser.error("--image-version must fit in a uint32_t")

    priv_pem = args.priv_in.read_bytes()
    priv_key = serialization.load_pem_private_key(priv_pem, password=None)
    if not isinstance(priv_key, ec.EllipticCurvePrivateKey) or not isinstance(
        priv_key.curve, ec.SECP256R1
    ):
        parser.error(f"{args.priv_in}: expected an ECDSA P-256 private key")

    payload = args.payload_in.read_bytes()
    image_type = IMAGE_TYPE_NAMES[args.image_type]

    signed = build_signed_image(
        priv_key,
        payload,
        image_type=image_type,
        image_version=args.image_version,
    )
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(signed)

    print(f"signed image -> {args.out}")
    print(f"  payload size   : {len(payload)} bytes")
    print(f"  total size     : {len(signed)} bytes "
          f"(header={fmt.IMG_HEADER_SIZE}, payload_offset={fmt.IMG_PAYLOAD_OFFSET_DEFAULT})")
    print(f"  payload sha256 : {hashlib.sha256(payload).hexdigest()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
