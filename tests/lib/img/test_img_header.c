#include "img_header.h"
#include "unity.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/*
 * Build a valid header into raw_buf with the trailing header_crc field
 * computed correctly. Tests then mutate raw_buf to exercise failure paths.
 */
static void build_valid_header_buf(uint8_t *raw_buf, img_header_t *seed)
{
    seed->magic           = IMG_HEADER_MAGIC;
    seed->header_version  = IMG_HEADER_VERSION;
    seed->image_version   = 0x00010203u;
    seed->image_type      = IMG_TYPE_APP;
    seed->payload_size    = 1024u;
    seed->payload_offset  = sizeof(img_header_t);

    for (int i = 0; i < IMG_SHA256_SIZE; ++i) {
        seed->sha256[i] = (uint8_t)i;
    }
    for (int i = 0; i < IMG_SIGNATURE_SIZE; ++i) {
        seed->signature[i] = (uint8_t)(0x80u + i);
    }
    for (int i = 0; i < 4; ++i) {
        seed->reserved[i] = 0;
    }
    seed->header_crc = 0; /* placeholder; recomputed below */

    memcpy(raw_buf, seed, sizeof(img_header_t));

    const size_t crc_offset = sizeof(img_header_t) - sizeof(uint32_t);
    uint32_t crc = img_crc32(raw_buf, crc_offset);
    memcpy(raw_buf + crc_offset, &crc, sizeof(crc));
    seed->header_crc = crc;
}

static void build_valid_metadata_buf(uint8_t *raw_buf, img_slot_metadata_t *seed)
{
    seed->magic             = IMG_SLOT_METADATA_MAGIC;
    seed->metadata_version  = IMG_SLOT_METADATA_VERSION;
    seed->active            = 1u;
    seed->fail_count        = 0u;
    seed->monotonic_counter = 7u;
    for (int i = 0; i < 3; ++i) {
        seed->reserved[i] = 0;
    }
    seed->metadata_crc = 0;

    memcpy(raw_buf, seed, sizeof(img_slot_metadata_t));

    const size_t crc_offset = sizeof(img_slot_metadata_t) - sizeof(uint32_t);
    uint32_t crc = img_crc32(raw_buf, crc_offset);
    memcpy(raw_buf + crc_offset, &crc, sizeof(crc));
    seed->metadata_crc = crc;
}

/* ---------- CRC-32 ---------- */

void test_crc32_known_vector_123456789(void)
{
    /* Standard reference: CRC-32 of "123456789" = 0xCBF43926. */
    const uint8_t v[] = "123456789";
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, img_crc32(v, sizeof(v) - 1));
}

void test_crc32_empty_buffer_is_zero(void)
{
    /* CRC-32 of zero-length input is 0 (init XOR final XOR cancel out). */
    const uint8_t v[1] = {0};
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, img_crc32(v, 0));
}

/* ---------- Header round-trip ---------- */

void test_header_round_trip(void)
{
    uint8_t raw[sizeof(img_header_t)];
    img_header_t seed;
    build_valid_header_buf(raw, &seed);

    img_header_t out;
    memset(&out, 0, sizeof(out));

    TEST_ASSERT_EQUAL_INT(IMG_OK, img_header_parse(raw, sizeof(raw), &out));
    TEST_ASSERT_EQUAL_HEX32(IMG_HEADER_MAGIC, out.magic);
    TEST_ASSERT_EQUAL_UINT32(IMG_HEADER_VERSION, out.header_version);
    TEST_ASSERT_EQUAL_UINT32(seed.image_version, out.image_version);
    TEST_ASSERT_EQUAL_UINT32(IMG_TYPE_APP, out.image_type);
    TEST_ASSERT_EQUAL_UINT32(seed.payload_size, out.payload_size);
    TEST_ASSERT_EQUAL_UINT32(seed.payload_offset, out.payload_offset);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(seed.sha256, out.sha256, IMG_SHA256_SIZE);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(seed.signature, out.signature, IMG_SIGNATURE_SIZE);
    TEST_ASSERT_EQUAL_UINT32(seed.header_crc, out.header_crc);
}

void test_header_accepts_bootloader_type(void)
{
    uint8_t raw[sizeof(img_header_t)];
    img_header_t seed;
    build_valid_header_buf(raw, &seed);

    /* Override image_type to BOOTLOADER and recompute the CRC. */
    uint32_t bootloader_type = IMG_TYPE_BOOTLOADER;
    memcpy(raw + offsetof(img_header_t, image_type), &bootloader_type,
           sizeof(bootloader_type));
    const size_t crc_offset = sizeof(img_header_t) - sizeof(uint32_t);
    uint32_t crc = img_crc32(raw, crc_offset);
    memcpy(raw + crc_offset, &crc, sizeof(crc));

    img_header_t out;
    TEST_ASSERT_EQUAL_INT(IMG_OK, img_header_parse(raw, sizeof(raw), &out));
    TEST_ASSERT_EQUAL_UINT32(IMG_TYPE_BOOTLOADER, out.image_type);
}

/* ---------- Header failure paths ---------- */

void test_header_buffer_too_small(void)
{
    uint8_t raw[sizeof(img_header_t)];
    img_header_t seed;
    build_valid_header_buf(raw, &seed);

    img_header_t out;
    TEST_ASSERT_EQUAL_INT(IMG_ERR_BAD_SIZE,
                          img_header_parse(raw, sizeof(img_header_t) - 1, &out));
}

void test_header_zero_length_buffer(void)
{
    uint8_t raw[sizeof(img_header_t)];
    img_header_t seed;
    build_valid_header_buf(raw, &seed);

    img_header_t out;
    TEST_ASSERT_EQUAL_INT(IMG_ERR_BAD_SIZE, img_header_parse(raw, 0, &out));
}

void test_header_bad_magic(void)
{
    uint8_t raw[sizeof(img_header_t)];
    img_header_t seed;
    build_valid_header_buf(raw, &seed);

    /* Replace magic, then recompute the CRC so we reach the magic check. */
    uint32_t bogus = 0xDEADBEEFu;
    memcpy(raw, &bogus, sizeof(bogus));
    const size_t crc_offset = sizeof(img_header_t) - sizeof(uint32_t);
    uint32_t crc = img_crc32(raw, crc_offset);
    memcpy(raw + crc_offset, &crc, sizeof(crc));

    img_header_t out;
    TEST_ASSERT_EQUAL_INT(IMG_ERR_BAD_MAGIC,
                          img_header_parse(raw, sizeof(raw), &out));
}

void test_header_bad_version_zero(void)
{
    uint8_t raw[sizeof(img_header_t)];
    img_header_t seed;
    build_valid_header_buf(raw, &seed);

    uint32_t bad = 0u;
    memcpy(raw + offsetof(img_header_t, header_version), &bad, sizeof(bad));
    const size_t crc_offset = sizeof(img_header_t) - sizeof(uint32_t);
    uint32_t crc = img_crc32(raw, crc_offset);
    memcpy(raw + crc_offset, &crc, sizeof(crc));

    img_header_t out;
    TEST_ASSERT_EQUAL_INT(IMG_ERR_BAD_VERSION,
                          img_header_parse(raw, sizeof(raw), &out));
}

void test_header_bad_version_99(void)
{
    uint8_t raw[sizeof(img_header_t)];
    img_header_t seed;
    build_valid_header_buf(raw, &seed);

    uint32_t bad = 99u;
    memcpy(raw + offsetof(img_header_t, header_version), &bad, sizeof(bad));
    const size_t crc_offset = sizeof(img_header_t) - sizeof(uint32_t);
    uint32_t crc = img_crc32(raw, crc_offset);
    memcpy(raw + crc_offset, &crc, sizeof(crc));

    img_header_t out;
    TEST_ASSERT_EQUAL_INT(IMG_ERR_BAD_VERSION,
                          img_header_parse(raw, sizeof(raw), &out));
}

void test_header_bad_image_type(void)
{
    uint8_t raw[sizeof(img_header_t)];
    img_header_t seed;
    build_valid_header_buf(raw, &seed);

    uint32_t bad = 0xFFu;
    memcpy(raw + offsetof(img_header_t, image_type), &bad, sizeof(bad));
    const size_t crc_offset = sizeof(img_header_t) - sizeof(uint32_t);
    uint32_t crc = img_crc32(raw, crc_offset);
    memcpy(raw + crc_offset, &crc, sizeof(crc));

    img_header_t out;
    TEST_ASSERT_EQUAL_INT(IMG_ERR_BAD_TYPE,
                          img_header_parse(raw, sizeof(raw), &out));
}

void test_header_zero_image_type(void)
{
    uint8_t raw[sizeof(img_header_t)];
    img_header_t seed;
    build_valid_header_buf(raw, &seed);

    uint32_t bad = 0u;
    memcpy(raw + offsetof(img_header_t, image_type), &bad, sizeof(bad));
    const size_t crc_offset = sizeof(img_header_t) - sizeof(uint32_t);
    uint32_t crc = img_crc32(raw, crc_offset);
    memcpy(raw + crc_offset, &crc, sizeof(crc));

    img_header_t out;
    TEST_ASSERT_EQUAL_INT(IMG_ERR_BAD_TYPE,
                          img_header_parse(raw, sizeof(raw), &out));
}

void test_header_payload_offset_too_small(void)
{
    uint8_t raw[sizeof(img_header_t)];
    img_header_t seed;
    build_valid_header_buf(raw, &seed);

    /* offset less than the header itself overlaps the header. */
    uint32_t bad = (uint32_t)(sizeof(img_header_t) - 1);
    memcpy(raw + offsetof(img_header_t, payload_offset), &bad, sizeof(bad));
    const size_t crc_offset = sizeof(img_header_t) - sizeof(uint32_t);
    uint32_t crc = img_crc32(raw, crc_offset);
    memcpy(raw + crc_offset, &crc, sizeof(crc));

    img_header_t out;
    TEST_ASSERT_EQUAL_INT(IMG_ERR_BAD_OFFSET,
                          img_header_parse(raw, sizeof(raw), &out));
}

void test_header_zero_payload_size(void)
{
    uint8_t raw[sizeof(img_header_t)];
    img_header_t seed;
    build_valid_header_buf(raw, &seed);

    uint32_t bad = 0u;
    memcpy(raw + offsetof(img_header_t, payload_size), &bad, sizeof(bad));
    const size_t crc_offset = sizeof(img_header_t) - sizeof(uint32_t);
    uint32_t crc = img_crc32(raw, crc_offset);
    memcpy(raw + crc_offset, &crc, sizeof(crc));

    img_header_t out;
    TEST_ASSERT_EQUAL_INT(IMG_ERR_BAD_SIZE,
                          img_header_parse(raw, sizeof(raw), &out));
}

void test_header_tampered_byte_fails_crc(void)
{
    uint8_t raw[sizeof(img_header_t)];
    img_header_t seed;
    build_valid_header_buf(raw, &seed);

    /* Flip one byte deep in the signature region. CRC must catch it. */
    raw[offsetof(img_header_t, signature) + 5] ^= 0xA5u;

    img_header_t out;
    TEST_ASSERT_EQUAL_INT(IMG_ERR_BAD_CRC,
                          img_header_parse(raw, sizeof(raw), &out));
}

void test_header_null_buf(void)
{
    img_header_t out;
    TEST_ASSERT_EQUAL_INT(IMG_ERR_NULL_ARG,
                          img_header_parse(NULL, sizeof(img_header_t), &out));
}

void test_header_null_out(void)
{
    uint8_t raw[sizeof(img_header_t)];
    img_header_t seed;
    build_valid_header_buf(raw, &seed);

    TEST_ASSERT_EQUAL_INT(IMG_ERR_NULL_ARG,
                          img_header_parse(raw, sizeof(raw), NULL));
}

/* ---------- Slot metadata round-trip ---------- */

void test_metadata_round_trip(void)
{
    uint8_t raw[sizeof(img_slot_metadata_t)];
    img_slot_metadata_t seed;
    build_valid_metadata_buf(raw, &seed);

    img_slot_metadata_t out;
    memset(&out, 0, sizeof(out));

    TEST_ASSERT_EQUAL_INT(IMG_OK,
                          img_slot_metadata_parse(raw, sizeof(raw), &out));
    TEST_ASSERT_EQUAL_HEX32(IMG_SLOT_METADATA_MAGIC, out.magic);
    TEST_ASSERT_EQUAL_UINT32(IMG_SLOT_METADATA_VERSION, out.metadata_version);
    TEST_ASSERT_EQUAL_UINT32(seed.active, out.active);
    TEST_ASSERT_EQUAL_UINT32(seed.fail_count, out.fail_count);
    TEST_ASSERT_EQUAL_UINT32(seed.monotonic_counter, out.monotonic_counter);
    TEST_ASSERT_EQUAL_UINT32(seed.metadata_crc, out.metadata_crc);
}

/* ---------- Slot metadata failure paths ---------- */

void test_metadata_buffer_too_small(void)
{
    uint8_t raw[sizeof(img_slot_metadata_t)];
    img_slot_metadata_t seed;
    build_valid_metadata_buf(raw, &seed);

    img_slot_metadata_t out;
    TEST_ASSERT_EQUAL_INT(IMG_ERR_BAD_SIZE,
                          img_slot_metadata_parse(raw,
                                                  sizeof(img_slot_metadata_t) - 1,
                                                  &out));
}

void test_metadata_bad_magic(void)
{
    uint8_t raw[sizeof(img_slot_metadata_t)];
    img_slot_metadata_t seed;
    build_valid_metadata_buf(raw, &seed);

    uint32_t bogus = 0xCAFEBABEu;
    memcpy(raw, &bogus, sizeof(bogus));
    const size_t crc_offset = sizeof(img_slot_metadata_t) - sizeof(uint32_t);
    uint32_t crc = img_crc32(raw, crc_offset);
    memcpy(raw + crc_offset, &crc, sizeof(crc));

    img_slot_metadata_t out;
    TEST_ASSERT_EQUAL_INT(IMG_ERR_BAD_MAGIC,
                          img_slot_metadata_parse(raw, sizeof(raw), &out));
}

void test_metadata_bad_version(void)
{
    uint8_t raw[sizeof(img_slot_metadata_t)];
    img_slot_metadata_t seed;
    build_valid_metadata_buf(raw, &seed);

    uint32_t bad = 99u;
    memcpy(raw + offsetof(img_slot_metadata_t, metadata_version), &bad,
           sizeof(bad));
    const size_t crc_offset = sizeof(img_slot_metadata_t) - sizeof(uint32_t);
    uint32_t crc = img_crc32(raw, crc_offset);
    memcpy(raw + crc_offset, &crc, sizeof(crc));

    img_slot_metadata_t out;
    TEST_ASSERT_EQUAL_INT(IMG_ERR_BAD_VERSION,
                          img_slot_metadata_parse(raw, sizeof(raw), &out));
}

void test_metadata_tampered_byte_fails_crc(void)
{
    uint8_t raw[sizeof(img_slot_metadata_t)];
    img_slot_metadata_t seed;
    build_valid_metadata_buf(raw, &seed);

    raw[offsetof(img_slot_metadata_t, fail_count)] ^= 0x55u;

    img_slot_metadata_t out;
    TEST_ASSERT_EQUAL_INT(IMG_ERR_BAD_CRC,
                          img_slot_metadata_parse(raw, sizeof(raw), &out));
}

void test_metadata_null_buf(void)
{
    img_slot_metadata_t out;
    TEST_ASSERT_EQUAL_INT(IMG_ERR_NULL_ARG,
                          img_slot_metadata_parse(NULL,
                                                  sizeof(img_slot_metadata_t),
                                                  &out));
}

void test_metadata_null_out(void)
{
    uint8_t raw[sizeof(img_slot_metadata_t)];
    img_slot_metadata_t seed;
    build_valid_metadata_buf(raw, &seed);

    TEST_ASSERT_EQUAL_INT(IMG_ERR_NULL_ARG,
                          img_slot_metadata_parse(raw, sizeof(raw), NULL));
}

int main(void)
{
    UNITY_BEGIN();
    /* CRC */
    RUN_TEST(test_crc32_known_vector_123456789);
    RUN_TEST(test_crc32_empty_buffer_is_zero);
    /* Header */
    RUN_TEST(test_header_round_trip);
    RUN_TEST(test_header_accepts_bootloader_type);
    RUN_TEST(test_header_buffer_too_small);
    RUN_TEST(test_header_zero_length_buffer);
    RUN_TEST(test_header_bad_magic);
    RUN_TEST(test_header_bad_version_zero);
    RUN_TEST(test_header_bad_version_99);
    RUN_TEST(test_header_bad_image_type);
    RUN_TEST(test_header_zero_image_type);
    RUN_TEST(test_header_payload_offset_too_small);
    RUN_TEST(test_header_zero_payload_size);
    RUN_TEST(test_header_tampered_byte_fails_crc);
    RUN_TEST(test_header_null_buf);
    RUN_TEST(test_header_null_out);
    /* Metadata */
    RUN_TEST(test_metadata_round_trip);
    RUN_TEST(test_metadata_buffer_too_small);
    RUN_TEST(test_metadata_bad_magic);
    RUN_TEST(test_metadata_bad_version);
    RUN_TEST(test_metadata_tampered_byte_fails_crc);
    RUN_TEST(test_metadata_null_buf);
    RUN_TEST(test_metadata_null_out);
    return UNITY_END();
}
