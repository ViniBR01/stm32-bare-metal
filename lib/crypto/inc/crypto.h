/*
 * lib/crypto — minimal crypto primitives for the bootloader path.
 *
 * Wraps:
 *   - SHA-256: B-Con's public-domain single-file implementation
 *              (see 3rd_party/sha256/).
 *   - ECDSA-P256 verify: micro-ecc, BSD 2-Clause, by Kenneth MacKay
 *              (see 3rd_party/micro-ecc/).
 *
 * Plus a constant-time byte compare for signature/digest equality checks.
 *
 * No dynamic allocation. Stack buffers only. Compiles for both host gcc and
 * arm-none-eabi-gcc (Cortex-M4F).
 */
#ifndef LIB_CRYPTO_H
#define LIB_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRYPTO_SHA256_DIGEST_LEN     32
#define CRYPTO_ECDSA_P256_PUBKEY_LEN 64  /* uncompressed: X (32) || Y (32) */
#define CRYPTO_ECDSA_P256_SIG_LEN    64  /* R (32) || S (32) */

/*
 * Compute the SHA-256 digest of `len` bytes at `buf` and write
 * CRYPTO_SHA256_DIGEST_LEN bytes to `out`. `buf` may be NULL iff `len == 0`.
 */
void crypto_sha256(const uint8_t *buf, size_t len,
                   uint8_t out[CRYPTO_SHA256_DIGEST_LEN]);

/*
 * Verify an ECDSA-P256 (secp256r1) signature.
 *   pubkey : uncompressed public key, X || Y, 32 bytes each (64 bytes total)
 *   hash   : SHA-256 digest of the message (32 bytes)
 *   sig    : signature R || S, 32 bytes each (64 bytes total)
 * Returns 1 on valid signature, 0 on invalid signature.
 */
int crypto_ecdsa_p256_verify(const uint8_t pubkey[CRYPTO_ECDSA_P256_PUBKEY_LEN],
                             const uint8_t hash[CRYPTO_SHA256_DIGEST_LEN],
                             const uint8_t sig[CRYPTO_ECDSA_P256_SIG_LEN]);

/*
 * Constant-time byte comparison. Returns 0 iff the first `len` bytes of
 * `a` and `b` are equal; otherwise returns a non-zero value. The execution
 * path does not depend on the data, only on `len` (no early exit).
 */
int crypto_memcmp_ct(const void *a, const void *b, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* LIB_CRYPTO_H */
