/*
 * Host unit tests for lib/crypto.
 *
 *   - SHA-256 known-answer tests from FIPS 180-4 §B.1 / §B.2 / §B.3.
 *   - ECDSA-P256 verify tests, two valid + two tampered vectors generated
 *     by tests/lib/crypto/vectors/gen_vectors.py and pinned in
 *     fips_186_4_p256.h. The verify procedure exercised matches FIPS
 *     186-4 §6.4.
 *   - crypto_memcmp_ct (constant-time byte compare) corner cases.
 *
 * Tests run on the host with native gcc; lib/crypto's portable C path is
 * the same code that runs on Cortex-M4F in Phase 1.6.
 */
#include "crypto.h"
#include "unity.h"

#include "vectors/fips_180_4_sha256.h"
#include "vectors/fips_186_4_p256.h"

#include <stdint.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* -------------------------------------------------------------------------- */
/* SHA-256                                                                    */
/* -------------------------------------------------------------------------- */

static void run_sha256_kat(const uint8_t *msg, size_t len,
                           const uint8_t expect[CRYPTO_SHA256_DIGEST_LEN])
{
    uint8_t out[CRYPTO_SHA256_DIGEST_LEN];
    crypto_sha256(msg, len, out);
    TEST_ASSERT_EQUAL_MEMORY(expect, out, CRYPTO_SHA256_DIGEST_LEN);
}

static void test_sha256_empty(void)
{
    run_sha256_kat((const uint8_t *)"", 0, fips_180_4_sha256_empty);
}

static void test_sha256_abc(void)
{
    run_sha256_kat((const uint8_t *)"abc", 3, fips_180_4_sha256_abc);
}

static void test_sha256_56byte_two_block(void)
{
    const char *msg = FIPS_180_4_SHA256_56BYTE_MSG;
    run_sha256_kat((const uint8_t *)msg, strlen(msg),
                   fips_180_4_sha256_56byte);
}

/* Streamed: hash one million 'a's via repeated chunked updates,
 * matching FIPS 180-4 §B.3. We don't dare allocate 1 MB on the stack —
 * use a 1 KB staging buffer and call crypto_sha256 indirectly by
 * driving the underlying primitive via a public API surface (we don't
 * have streaming public API, so use a private path: simulate streaming
 * by chunking through the sha256 context exposed via 3rd_party/sha256.) */
#include "sha256.h"
static void test_sha256_one_million_a(void)
{
    SHA256_CTX ctx;
    sha256_init(&ctx);
    uint8_t chunk[1024];
    memset(chunk, 'a', sizeof(chunk));
    size_t remaining = 1000000;
    while (remaining > 0) {
        size_t n = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
        sha256_update(&ctx, chunk, n);
        remaining -= n;
    }
    uint8_t out[CRYPTO_SHA256_DIGEST_LEN];
    sha256_final(&ctx, out);
    TEST_ASSERT_EQUAL_MEMORY(fips_180_4_sha256_million_a, out,
                             CRYPTO_SHA256_DIGEST_LEN);
}

static void test_sha256_single_byte(void)
{
    /* SHA-256("a") = ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb */
    static const uint8_t expect[32] = {
        0xca, 0x97, 0x81, 0x12, 0xca, 0x1b, 0xbd, 0xca,
        0xfa, 0xc2, 0x31, 0xb3, 0x9a, 0x23, 0xdc, 0x4d,
        0xa7, 0x86, 0xef, 0xf8, 0x14, 0x7c, 0x4e, 0x72,
        0xb9, 0x80, 0x77, 0x85, 0xaf, 0xee, 0x48, 0xbb,
    };
    run_sha256_kat((const uint8_t *)"a", 1, expect);
}

static void test_sha256_chunked_update_matches_one_shot(void)
{
    /* Hashing "abc" in three 1-byte updates must match a single 3-byte
     * call. Exercises the update/final boundary handling. */
    uint8_t one_shot[CRYPTO_SHA256_DIGEST_LEN];
    crypto_sha256((const uint8_t *)"abc", 3, one_shot);

    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)"a", 1);
    sha256_update(&ctx, (const uint8_t *)"b", 1);
    sha256_update(&ctx, (const uint8_t *)"c", 1);
    uint8_t streamed[CRYPTO_SHA256_DIGEST_LEN];
    sha256_final(&ctx, streamed);

    TEST_ASSERT_EQUAL_MEMORY(one_shot, streamed, CRYPTO_SHA256_DIGEST_LEN);
}

/* -------------------------------------------------------------------------- */
/* ECDSA-P256 verify                                                          */
/* -------------------------------------------------------------------------- */

static void run_p256_vector(const uint8_t pubkey[64],
                            const uint8_t hash[32],
                            const uint8_t sig[64],
                            int expect)
{
    int got = crypto_ecdsa_p256_verify(pubkey, hash, sig);
    TEST_ASSERT_EQUAL_INT(expect, got);
}

static void test_ecdsa_p256_v1_valid(void)
{
    run_p256_vector(p256_vec_v1_pubkey, p256_vec_v1_hash, p256_vec_v1_sig,
                    P256_VEC_V1_EXPECT);
}

static void test_ecdsa_p256_v2_valid(void)
{
    run_p256_vector(p256_vec_v2_pubkey, p256_vec_v2_hash, p256_vec_v2_sig,
                    P256_VEC_V2_EXPECT);
}

static void test_ecdsa_p256_v3_tampered_hash(void)
{
    run_p256_vector(p256_vec_v3_pubkey, p256_vec_v3_hash, p256_vec_v3_sig,
                    P256_VEC_V3_EXPECT);
}

static void test_ecdsa_p256_v4_tampered_signature(void)
{
    run_p256_vector(p256_vec_v4_pubkey, p256_vec_v4_hash, p256_vec_v4_sig,
                    P256_VEC_V4_EXPECT);
}

static void test_ecdsa_p256_v1_corrupt_pubkey_fails(void)
{
    /* Flip a bit in Qx and re-verify the otherwise-valid v1 vector. */
    uint8_t pk[64];
    memcpy(pk, p256_vec_v1_pubkey, sizeof(pk));
    pk[0] ^= 0x01;
    int got = crypto_ecdsa_p256_verify(pk, p256_vec_v1_hash,
                                       p256_vec_v1_sig);
    TEST_ASSERT_EQUAL_INT(0, got);
}

static void test_ecdsa_p256_v1_zero_signature_fails(void)
{
    uint8_t zero_sig[64] = {0};
    int got = crypto_ecdsa_p256_verify(p256_vec_v1_pubkey,
                                       p256_vec_v1_hash, zero_sig);
    TEST_ASSERT_EQUAL_INT(0, got);
}

/* -------------------------------------------------------------------------- */
/* crypto_memcmp_ct                                                           */
/* -------------------------------------------------------------------------- */

static void test_memcmp_ct_equal_returns_zero(void)
{
    uint8_t a[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    uint8_t b[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    TEST_ASSERT_EQUAL_INT(0, crypto_memcmp_ct(a, b, sizeof(a)));
}

static void test_memcmp_ct_zero_length_returns_zero(void)
{
    uint8_t a[1] = {0xaa};
    uint8_t b[1] = {0x55};
    TEST_ASSERT_EQUAL_INT(0, crypto_memcmp_ct(a, b, 0));
}

static void test_memcmp_ct_first_byte_differs_nonzero(void)
{
    uint8_t a[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t b[8] = {0, 2, 3, 4, 5, 6, 7, 8};
    TEST_ASSERT_NOT_EQUAL(0, crypto_memcmp_ct(a, b, sizeof(a)));
}

static void test_memcmp_ct_last_byte_differs_nonzero(void)
{
    uint8_t a[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t b[8] = {1, 2, 3, 4, 5, 6, 7, 9};
    TEST_ASSERT_NOT_EQUAL(0, crypto_memcmp_ct(a, b, sizeof(a)));
}

static void test_memcmp_ct_middle_byte_differs_nonzero(void)
{
    uint8_t a[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t b[8] = {1, 2, 3, 0xff, 5, 6, 7, 8};
    TEST_ASSERT_NOT_EQUAL(0, crypto_memcmp_ct(a, b, sizeof(a)));
}

static void test_memcmp_ct_multiple_bytes_differ_nonzero(void)
{
    uint8_t a[16] = {0};
    uint8_t b[16];
    memset(b, 0xff, sizeof(b));
    TEST_ASSERT_NOT_EQUAL(0, crypto_memcmp_ct(a, b, sizeof(a)));
}

static void test_memcmp_ct_one_bit_difference_nonzero(void)
{
    uint8_t a[32] = {0};
    uint8_t b[32] = {0};
    b[17] = 0x01;
    TEST_ASSERT_NOT_EQUAL(0, crypto_memcmp_ct(a, b, sizeof(a)));
}

static void test_memcmp_ct_all_zeros_equal(void)
{
    uint8_t a[64] = {0};
    uint8_t b[64] = {0};
    TEST_ASSERT_EQUAL_INT(0, crypto_memcmp_ct(a, b, sizeof(a)));
}

static void test_memcmp_ct_all_ff_equal(void)
{
    uint8_t a[64];
    uint8_t b[64];
    memset(a, 0xff, sizeof(a));
    memset(b, 0xff, sizeof(b));
    TEST_ASSERT_EQUAL_INT(0, crypto_memcmp_ct(a, b, sizeof(a)));
}

static void test_memcmp_ct_short_diff_nonzero(void)
{
    uint8_t a[1] = {0x00};
    uint8_t b[1] = {0x80};
    TEST_ASSERT_NOT_EQUAL(0, crypto_memcmp_ct(a, b, sizeof(a)));
}

static void test_memcmp_ct_short_eq_zero(void)
{
    uint8_t a[1] = {0x42};
    uint8_t b[1] = {0x42};
    TEST_ASSERT_EQUAL_INT(0, crypto_memcmp_ct(a, b, sizeof(a)));
}

static void test_memcmp_ct_signature_length_eq(void)
{
    /* The realistic call shape: comparing a 64-byte ECDSA signature. */
    uint8_t a[64];
    uint8_t b[64];
    for (size_t i = 0; i < sizeof(a); i++) {
        a[i] = (uint8_t)(i * 7 + 13);
        b[i] = a[i];
    }
    TEST_ASSERT_EQUAL_INT(0, crypto_memcmp_ct(a, b, sizeof(a)));
}

static void test_memcmp_ct_signature_length_diff(void)
{
    uint8_t a[64];
    uint8_t b[64];
    for (size_t i = 0; i < sizeof(a); i++) {
        a[i] = (uint8_t)(i * 7 + 13);
        b[i] = a[i];
    }
    b[63] ^= 0x80;  /* flip MSB of last byte */
    TEST_ASSERT_NOT_EQUAL(0, crypto_memcmp_ct(a, b, sizeof(a)));
}

/* -------------------------------------------------------------------------- */
/* main                                                                       */
/* -------------------------------------------------------------------------- */

int main(void)
{
    UNITY_BEGIN();

    /* SHA-256 */
    RUN_TEST(test_sha256_empty);
    RUN_TEST(test_sha256_abc);
    RUN_TEST(test_sha256_single_byte);
    RUN_TEST(test_sha256_56byte_two_block);
    RUN_TEST(test_sha256_one_million_a);
    RUN_TEST(test_sha256_chunked_update_matches_one_shot);

    /* ECDSA-P256 verify */
    RUN_TEST(test_ecdsa_p256_v1_valid);
    RUN_TEST(test_ecdsa_p256_v2_valid);
    RUN_TEST(test_ecdsa_p256_v3_tampered_hash);
    RUN_TEST(test_ecdsa_p256_v4_tampered_signature);
    RUN_TEST(test_ecdsa_p256_v1_corrupt_pubkey_fails);
    RUN_TEST(test_ecdsa_p256_v1_zero_signature_fails);

    /* constant-time memcmp */
    RUN_TEST(test_memcmp_ct_equal_returns_zero);
    RUN_TEST(test_memcmp_ct_zero_length_returns_zero);
    RUN_TEST(test_memcmp_ct_first_byte_differs_nonzero);
    RUN_TEST(test_memcmp_ct_last_byte_differs_nonzero);
    RUN_TEST(test_memcmp_ct_middle_byte_differs_nonzero);
    RUN_TEST(test_memcmp_ct_multiple_bytes_differ_nonzero);
    RUN_TEST(test_memcmp_ct_one_bit_difference_nonzero);
    RUN_TEST(test_memcmp_ct_all_zeros_equal);
    RUN_TEST(test_memcmp_ct_all_ff_equal);
    RUN_TEST(test_memcmp_ct_short_diff_nonzero);
    RUN_TEST(test_memcmp_ct_short_eq_zero);
    RUN_TEST(test_memcmp_ct_signature_length_eq);
    RUN_TEST(test_memcmp_ct_signature_length_diff);

    return UNITY_END();
}
