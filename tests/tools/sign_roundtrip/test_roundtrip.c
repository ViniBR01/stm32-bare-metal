/*
 * Round-trip test: Python tools/{keygen,sign_image}.py produce a signed
 * image; the real C parser (lib/img) and crypto verifier (lib/crypto) consume
 * it. Any byte-level drift between the two sides shows up here.
 *
 * The Makefile produces $(SIGNED_IMAGE) at build time and passes its path via
 * -DSIGNED_IMAGE_PATH. The bootloader_pubkey symbol is supplied by the
 * generated test_pubkey.c, also compiled in.
 */

#include "img_header.h"
#include "crypto.h"
#include "unity.h"

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SIGNED_IMAGE_PATH
#error "SIGNED_IMAGE_PATH must be defined by the Makefile"
#endif

/* Provided by the generated build/test_pubkey.c. */
extern const uint8_t bootloader_pubkey[CRYPTO_ECDSA_P256_PUBKEY_LEN];

/* Loaded once in main(); each test makes its own working copy. */
static uint8_t *g_image = NULL;
static size_t g_image_len = 0;

void setUp(void) {}
void tearDown(void) {}

static uint8_t *load_image(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(f);
        return NULL;
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) {
        free(buf);
        return NULL;
    }
    *out_len = (size_t)sz;
    return buf;
}

/* Recompute and patch the trailing CRC of a header in-place. Used by the
 * tampering tests so the C parser reaches the field-validation path the test
 * actually wants to exercise (e.g. signature mismatch, not CRC mismatch). */
static void rewrite_header_crc(uint8_t *buf)
{
    const size_t crc_offset = sizeof(img_header_t) - sizeof(uint32_t);
    uint32_t crc = img_crc32(buf, crc_offset);
    memcpy(buf + crc_offset, &crc, sizeof(crc));
}

/* Happy path: parse Python-signed image, hash the payload, verify signature
 * against the embedded pubkey. This is exactly what the Phase 1.6 bootloader
 * will do on every boot. */
static void test_signed_image_parses_and_verifies(void)
{
    uint8_t *image = (uint8_t *)malloc(g_image_len);
    TEST_ASSERT_NOT_NULL(image);
    memcpy(image, g_image, g_image_len);

    img_header_t hdr;
    TEST_ASSERT_EQUAL_INT(IMG_OK,
                          img_header_parse(image, g_image_len, &hdr));

    TEST_ASSERT_EQUAL_HEX32(IMG_HEADER_MAGIC, hdr.magic);
    TEST_ASSERT_EQUAL_UINT32(IMG_HEADER_VERSION, hdr.header_version);
    TEST_ASSERT_EQUAL_UINT32(IMG_TYPE_APP, hdr.image_type);
    TEST_ASSERT_EQUAL_UINT32(7u, hdr.image_version);
    TEST_ASSERT_EQUAL_UINT32(256u, hdr.payload_size);
    TEST_ASSERT_EQUAL_UINT32(sizeof(img_header_t), hdr.payload_offset);
    TEST_ASSERT_EQUAL_size_t(sizeof(img_header_t) + 256u, g_image_len);

    /* Recompute the payload digest and confirm it matches what the signer
     * embedded. This is the integrity check side of the bootloader logic. */
    uint8_t digest[CRYPTO_SHA256_DIGEST_LEN];
    crypto_sha256(image + hdr.payload_offset, hdr.payload_size, digest);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(digest, hdr.sha256, CRYPTO_SHA256_DIGEST_LEN);

    /* Authenticity: the signature in the header verifies against the digest
     * under the embedded public key. */
    int ok = crypto_ecdsa_p256_verify(bootloader_pubkey, hdr.sha256, hdr.signature);
    TEST_ASSERT_EQUAL_INT(1, ok);

    free(image);
}

/* Flip a payload byte: header's recorded sha256 no longer matches the
 * (now-different) computed digest. Models a payload-corruption attack. */
static void test_tampered_payload_breaks_sha256_match(void)
{
    uint8_t *image = (uint8_t *)malloc(g_image_len);
    TEST_ASSERT_NOT_NULL(image);
    memcpy(image, g_image, g_image_len);

    img_header_t hdr;
    TEST_ASSERT_EQUAL_INT(IMG_OK,
                          img_header_parse(image, g_image_len, &hdr));

    /* Header is intact; just flip a payload bit. */
    image[hdr.payload_offset + 10] ^= 0x01u;

    uint8_t digest[CRYPTO_SHA256_DIGEST_LEN];
    crypto_sha256(image + hdr.payload_offset, hdr.payload_size, digest);
    TEST_ASSERT_NOT_EQUAL(0,
                          memcmp(digest, hdr.sha256, CRYPTO_SHA256_DIGEST_LEN));

    free(image);
}

/* Flip a signature byte and rewrite the header CRC so img_header_parse still
 * succeeds — the verifier is the one that must catch the forgery. */
static void test_tampered_signature_fails_verify(void)
{
    uint8_t *image = (uint8_t *)malloc(g_image_len);
    TEST_ASSERT_NOT_NULL(image);
    memcpy(image, g_image, g_image_len);

    /* signature[0] is at offset 24+32 = 56. */
    const size_t sig_offset =
        offsetof(img_header_t, signature);
    image[sig_offset] ^= 0x01u;
    rewrite_header_crc(image);

    img_header_t hdr;
    TEST_ASSERT_EQUAL_INT(IMG_OK,
                          img_header_parse(image, g_image_len, &hdr));

    int ok = crypto_ecdsa_p256_verify(bootloader_pubkey, hdr.sha256, hdr.signature);
    TEST_ASSERT_EQUAL_INT(0, ok);

    free(image);
}

/* Flip a non-CRC header byte without rewriting the CRC: parser must reject. */
static void test_tampered_header_fails_crc(void)
{
    uint8_t *image = (uint8_t *)malloc(g_image_len);
    TEST_ASSERT_NOT_NULL(image);
    memcpy(image, g_image, g_image_len);

    /* Flip the low byte of image_version (offset 8). Anywhere in [0, 136)
     * works — only the trailing 4 bytes are the CRC field itself. */
    image[8] ^= 0x80u;

    img_header_t hdr;
    TEST_ASSERT_EQUAL_INT(IMG_ERR_BAD_CRC,
                          img_header_parse(image, g_image_len, &hdr));

    free(image);
}

int main(void)
{
    g_image = load_image(SIGNED_IMAGE_PATH, &g_image_len);
    if (!g_image) {
        fprintf(stderr,
                "FATAL: could not load signed image at %s\n",
                SIGNED_IMAGE_PATH);
        return 1;
    }

    UNITY_BEGIN();
    RUN_TEST(test_signed_image_parses_and_verifies);
    RUN_TEST(test_tampered_payload_breaks_sha256_match);
    RUN_TEST(test_tampered_signature_fails_verify);
    RUN_TEST(test_tampered_header_fails_crc);
    int rc = UNITY_END();

    free(g_image);
    return rc;
}
