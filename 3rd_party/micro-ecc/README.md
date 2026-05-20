# micro-ecc (vendored)

This directory contains a trimmed copy of [kmackay/micro-ecc](https://github.com/kmackay/micro-ecc),
used by `lib/crypto/` for ECDSA-P256 signature verification in the bootloader (Plan 001).

See `VERSION.txt` for upstream commit, license, and the list of files we vendored
(and the asm includes we deliberately omitted).

## How it is built

`lib/crypto/Makefile` compiles `uECC.c` directly into `libcrypto.a` with these flags:

```
-DuECC_PLATFORM=0           # uECC_arch_other — portable C, no inline asm
-DuECC_SUPPORTS_secp160r1=0
-DuECC_SUPPORTS_secp192r1=0
-DuECC_SUPPORTS_secp224r1=0
-DuECC_SUPPORTS_secp256r1=1
-DuECC_SUPPORTS_secp256k1=0
-DuECC_SUPPORT_COMPRESSED_POINT=0
-DuECC_SQUARE_FUNC=0
```

We only ever call `uECC_verify()`; signing/keygen happen on the host (Phase 1.4).

## Why no submodule

The upstream tree carries asm files, test harnesses, and Python build glue we do
not need. Vendoring a curated subset keeps the build closed-form and reviewable.
