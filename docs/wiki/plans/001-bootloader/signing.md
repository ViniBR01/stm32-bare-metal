# Image Signing Workflow (Plan 001 Phase 1.4)

This page documents the host-side signing workflow that produces firmware
images consumable by the bootloader. The on-flash format is documented
separately in [image-format.md](image-format.md); this page covers the
*tools* that produce that format.

## Tools

| Tool | Purpose |
|---|---|
| [tools/keygen.py](../../../../tools/keygen.py) | Generate ECDSA P-256 keypair; emit `bootloader_pubkey.c` |
| [tools/sign_image.py](../../../../tools/sign_image.py) | Sign a `.bin` payload, produce a `.signed.bin` |
| [tools/_img_format.py](../../../../tools/_img_format.py) | Shared on-flash format spec (struct layout, CRC, magics) |

Both scripts depend on the Python `cryptography` library:

```sh
pip install cryptography
```

## End-to-end flow

```
                                                    ┌────────────────────────┐
                                                    │ apps/bootloader/loader │
                                                    │   bootloader_pubkey.c  │
                                                    └───────────▲────────────┘
                                                                │ committed
                                                                │
   ┌──────────────────┐    keygen.py                ┌────────────┴─┐
   │ OS RNG / --seed  ├────────────► (EC P-256) ───►│ priv key.pem │
   └──────────────────┘                             │ (gitignored) │
                                                    └────────┬─────┘
                                                             │
   ┌─────────────┐                                           │
   │ payload.bin ├────────────────►─sign_image.py◄───────────┘
   └─────────────┘                       │
                                         │
                                         ▼
                              ┌──────────────────────┐
                              │ payload.signed.bin   │
                              │ ┌─img_header_t (140)─┤
                              │ ├─payload (N)────────┤
                              │ └────────────────────┘
                              └──────────────────────┘
```

### 1. Generate a keypair

```sh
python3 tools/keygen.py \
    --priv-out keys/dev_priv.pem \
    --pub-out apps/bootloader/loader/bootloader_pubkey.c
```

`keygen.py` writes:

- A PKCS#8 unencrypted PEM private key (mode `0600`). The `.pem` extension is
  caught by `.gitignore` — never commit a private key.
- A C source defining `const uint8_t bootloader_pubkey[64]` with the raw
  uncompressed Qx || Qy public key (big-endian, 32 + 32 bytes, no `0x04`
  prefix). This matches `CRYPTO_ECDSA_P256_PUBKEY_LEN` in
  [lib/crypto/inc/crypto.h](../../../../lib/crypto/inc/crypto.h) and is
  consumed directly by `crypto_ecdsa_p256_verify()`.

Pass `--seed <string>` to derive the keypair deterministically from a seed
(used by the round-trip test fixture). Without `--seed`, the OS RNG is used.

### 2. Sign a payload

```sh
python3 tools/sign_image.py \
    --priv-in keys/dev_priv.pem \
    --in build/apps/cli/cli_simple/cli_simple.bin \
    --out build/cli_simple.signed.bin \
    --image-type app \
    --image-version 7
```

`sign_image.py`:

1. Computes SHA-256 of the payload.
2. Signs the digest with ECDSA P-256, decodes DER → raw R || S (64 bytes).
3. Packs the 140-byte `img_header_t` with magic, versions, payload size,
   `payload_offset = 140`, the digest, the signature, and a CRC-32 trailer
   over the preceding 136 bytes.
4. Writes header + payload to `--out`.

The CRC algorithm is IEEE 802.3 / zlib (polynomial `0xEDB88320`, init
`0xFFFFFFFF`, reflected, final XOR `0xFFFFFFFF`) and is implemented in
`tools/_img_format.py::crc32` to match
[lib/img/src/img_header.c](../../../../lib/img/src/img_header.c) bit-for-bit.

### 3. Flash and boot

(Phase 1.5 / 1.6 work — for now the signed image just sits on disk.) The
bootloader will read the header from the slot, verify the CRC, hash the
payload, and call `crypto_ecdsa_p256_verify()` against the embedded public
key. On verify success it jumps to the payload reset vector.

## Cross-language compatibility

The signer (Python) and the parser (C) must agree on every byte. We prove
this on every CI build with the round-trip suite at
[tests/tools/sign_roundtrip/](../../../../tests/tools/sign_roundtrip/):

1. The test Makefile invokes `keygen.py` to produce a keypair from a fixed
   seed.
2. It then invokes `sign_image.py` to sign the committed
   `fixture_payload.bin`.
3. A Unity test compiles in the generated `bootloader_pubkey.c`, reads the
   `.signed.bin`, and runs four cases: happy path, tampered payload (digest
   mismatch), tampered signature (verify returns 0), tampered header (CRC
   mismatch).

If the Python tools and the C parser drift, this suite fails before any PR
can land.

## Sanity checks

### Selftest the format module

```sh
python3 tools/_img_format.py
```

Confirms the CRC reference vector (`crc32("123456789") == 0xCBF43926`) and
the pinned struct sizes (140 / 36 bytes).

### OpenSSL cross-check

The signer uses Python `cryptography` (which wraps OpenSSL); to
independently confirm the signature is valid you can re-derive the
DER-encoded form and verify with `openssl`:

```sh
# Derive the SHA-256 of the payload.
python3 -c "
import hashlib, sys
print(hashlib.sha256(open('payload.bin','rb').read()).hexdigest())
"

# Convert the raw R||S signature inside the .signed.bin to DER, then verify.
# (Out of scope for the standard workflow — the round-trip test already gives
# higher-confidence coverage by exercising the real C verifier.)
```

In practice, the round-trip suite is the authoritative cross-check. OpenSSL
verification is useful only when debugging cryptography-library behaviour
itself.

## Production-gap notes

This workflow is for development and learning. A production signing
pipeline would additionally:

- Store the signing key in an HSM or KMS, not on disk.
- Require multi-party authorisation (M-of-N) per signing operation.
- Sign in a hardened environment with audit logs.
- Bind every signed image to an immutable provenance record.

Plan 001 Phase 1.11 (threat model + production-gap docs) covers the gap in
detail.
