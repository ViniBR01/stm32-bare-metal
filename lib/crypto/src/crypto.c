#include "crypto.h"

#include "sha256.h"
#include "uECC.h"

void crypto_sha256(const uint8_t *buf, size_t len,
                   uint8_t out[CRYPTO_SHA256_DIGEST_LEN])
{
    SHA256_CTX ctx;
    sha256_init(&ctx);
    if (len > 0) {
        sha256_update(&ctx, buf, len);
    }
    sha256_final(&ctx, out);
}

int crypto_ecdsa_p256_verify(const uint8_t pubkey[CRYPTO_ECDSA_P256_PUBKEY_LEN],
                             const uint8_t hash[CRYPTO_SHA256_DIGEST_LEN],
                             const uint8_t sig[CRYPTO_ECDSA_P256_SIG_LEN])
{
    /* uECC_verify returns 1 on success, 0 on failure. */
    return uECC_verify(pubkey, hash, CRYPTO_SHA256_DIGEST_LEN, sig,
                       uECC_secp256r1());
}

int crypto_memcmp_ct(const void *a, const void *b, size_t len)
{
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= (uint8_t)(pa[i] ^ pb[i]);
    }
    return (int)diff;
}
