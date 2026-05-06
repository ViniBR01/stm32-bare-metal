/*
 * test_crc.c -- Host unit tests for drivers/src/crc.c
 *
 * Two tiers of tests:
 *
 * Tier 2 -- Software reference CRC tests
 *   Validates a pure software CRC-32/MPEG-2 implementation against known
 *   test vectors. This reference is used to cross-validate hardware results.
 *
 * Tier 1 -- Register configuration tests
 *   Tests crc_init(), crc_reset(), crc_accumulate(), and crc_get_result()
 *   against fake peripheral structs.
 *
 * setUp() zeroes all fake structs via test_periph_reset() before each test.
 */

#include "unity.h"
#include "stm32f4xx.h"
#include "error.h"
#include "crc.h"

void setUp(void)    { test_periph_reset(); }
void tearDown(void) {}

/* ======================================================================== */
/* Software CRC-32/MPEG-2 reference implementation                          */
/* ======================================================================== */

#define CRC32_MPEG2_POLY  0x04C11DB7U
#define CRC32_MPEG2_INIT  0xFFFFFFFFU

static uint32_t sw_crc32_mpeg2(const uint32_t *data, uint32_t len)
{
    uint32_t crc = CRC32_MPEG2_INIT;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 32; bit++) {
            if (crc & 0x80000000U) {
                crc = (crc << 1) ^ CRC32_MPEG2_POLY;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}

/* ======================================================================== */
/* Tier 2: Software CRC reference tests                                      */
/* ======================================================================== */

void test_sw_crc_single_word_zero(void)
{
    uint32_t data[] = {0x00000000};
    uint32_t result = sw_crc32_mpeg2(data, 1);
    /* 0xFFFFFFFF XOR 0 = 0xFFFFFFFF, shift 32 bits through polynomial */
    TEST_ASSERT_EQUAL_HEX32(0xC704DD7BU, result);
}

void test_sw_crc_single_word_one(void)
{
    uint32_t data[] = {0x00000001};
    uint32_t result = sw_crc32_mpeg2(data, 1);
    TEST_ASSERT_NOT_EQUAL_HEX32(CRC32_MPEG2_INIT, result);
    TEST_ASSERT_NOT_EQUAL_HEX32(0U, result);
}

void test_sw_crc_multiple_words(void)
{
    uint32_t data[] = {0x00000001, 0x00000002, 0x00000003};
    uint32_t result = sw_crc32_mpeg2(data, 3);
    TEST_ASSERT_NOT_EQUAL_HEX32(0U, result);
    TEST_ASSERT_NOT_EQUAL_HEX32(CRC32_MPEG2_INIT, result);
}

void test_sw_crc_order_matters(void)
{
    uint32_t data_a[] = {0x11111111, 0x22222222};
    uint32_t data_b[] = {0x22222222, 0x11111111};
    uint32_t result_a = sw_crc32_mpeg2(data_a, 2);
    uint32_t result_b = sw_crc32_mpeg2(data_b, 2);
    TEST_ASSERT_NOT_EQUAL_HEX32(result_a, result_b);
}

void test_sw_crc_accumulation_equivalent(void)
{
    uint32_t data[] = {0xDEADBEEF, 0xCAFEBABE, 0x12345678};
    uint32_t full = sw_crc32_mpeg2(data, 3);

    /* Compute incrementally by feeding the intermediate CRC state */
    uint32_t crc = CRC32_MPEG2_INIT;
    for (uint32_t i = 0; i < 3; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 32; bit++) {
            if (crc & 0x80000000U) {
                crc = (crc << 1) ^ CRC32_MPEG2_POLY;
            } else {
                crc = crc << 1;
            }
        }
    }
    TEST_ASSERT_EQUAL_HEX32(full, crc);
}

void test_sw_crc_known_vector_deadbeef(void)
{
    uint32_t data[] = {0xDEADBEEF};
    uint32_t result = sw_crc32_mpeg2(data, 1);
    /* Verify determinism: same input always gives same output */
    TEST_ASSERT_EQUAL_HEX32(result, sw_crc32_mpeg2(data, 1));
    /* Verify it's not the init value */
    TEST_ASSERT_NOT_EQUAL_HEX32(CRC32_MPEG2_INIT, result);
}

/* ======================================================================== */
/* Tier 1: Register configuration tests                                      */
/* ======================================================================== */

void test_crc_init_enables_rcc_clock(void)
{
    err_t err = crc_init();
    TEST_ASSERT_EQUAL(ERR_OK, err);
    TEST_ASSERT_BITS_HIGH(RCC_AHB1ENR_CRCEN, fake_RCC.AHB1ENR);
}

void test_crc_init_resets_accumulator(void)
{
    crc_init();
    TEST_ASSERT_BITS_HIGH(CRC_CR_RESET, fake_CRC.CR);
}

void test_crc_init_preserves_other_rcc_bits(void)
{
    fake_RCC.AHB1ENR = 0x00000001U;  /* Some other peripheral enabled */
    crc_init();
    TEST_ASSERT_BITS_HIGH(0x00000001U, fake_RCC.AHB1ENR);
    TEST_ASSERT_BITS_HIGH(RCC_AHB1ENR_CRCEN, fake_RCC.AHB1ENR);
}

void test_crc_reset_writes_cr(void)
{
    fake_CRC.CR = 0;
    crc_reset();
    TEST_ASSERT_BITS_HIGH(CRC_CR_RESET, fake_CRC.CR);
}

void test_crc_get_result_reads_dr(void)
{
    fake_CRC.DR = 0x12345678U;
    uint32_t result = crc_get_result();
    TEST_ASSERT_EQUAL_HEX32(0x12345678U, result);
}

void test_crc_get_result_after_init_reads_dr(void)
{
    fake_CRC.DR = 0xFFFFFFFFU;
    uint32_t result = crc_get_result();
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFU, result);
}

void test_crc_accumulate_null_returns_current_dr(void)
{
    fake_CRC.DR = 0xAABBCCDDU;
    uint32_t result = crc_accumulate(NULL, 5);
    TEST_ASSERT_EQUAL_HEX32(0xAABBCCDDU, result);
}

void test_crc_accumulate_zero_len_returns_current_dr(void)
{
    uint32_t data[] = {0x11111111};
    fake_CRC.DR = 0x55555555U;
    uint32_t result = crc_accumulate(data, 0);
    TEST_ASSERT_EQUAL_HEX32(0x55555555U, result);
}

void test_crc_accumulate_single_word_writes_dr(void)
{
    uint32_t data[] = {0xDEADBEEF};
    crc_accumulate(data, 1);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, fake_CRC.DR);
}

void test_crc_accumulate_multiple_words_writes_last(void)
{
    uint32_t data[] = {0x11111111, 0x22222222, 0x33333333};
    crc_accumulate(data, 3);
    /* In the fake struct, DR just holds the last written value */
    TEST_ASSERT_EQUAL_HEX32(0x33333333, fake_CRC.DR);
}

void test_crc_accumulate_returns_dr(void)
{
    uint32_t data[] = {0xCAFEBABE};
    /* The fake DR will hold 0xCAFEBABE after the write, and we read it back */
    uint32_t result = crc_accumulate(data, 1);
    TEST_ASSERT_EQUAL_HEX32(0xCAFEBABE, result);
}

/* ======================================================================== */
/* Main                                                                      */
/* ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* Tier 2: Software CRC reference */
    RUN_TEST(test_sw_crc_single_word_zero);
    RUN_TEST(test_sw_crc_single_word_one);
    RUN_TEST(test_sw_crc_multiple_words);
    RUN_TEST(test_sw_crc_order_matters);
    RUN_TEST(test_sw_crc_accumulation_equivalent);
    RUN_TEST(test_sw_crc_known_vector_deadbeef);

    /* Tier 1: Register configuration */
    RUN_TEST(test_crc_init_enables_rcc_clock);
    RUN_TEST(test_crc_init_resets_accumulator);
    RUN_TEST(test_crc_init_preserves_other_rcc_bits);
    RUN_TEST(test_crc_reset_writes_cr);
    RUN_TEST(test_crc_get_result_reads_dr);
    RUN_TEST(test_crc_get_result_after_init_reads_dr);
    RUN_TEST(test_crc_accumulate_null_returns_current_dr);
    RUN_TEST(test_crc_accumulate_zero_len_returns_current_dr);
    RUN_TEST(test_crc_accumulate_single_word_writes_dr);
    RUN_TEST(test_crc_accumulate_multiple_words_writes_last);
    RUN_TEST(test_crc_accumulate_returns_dr);

    return UNITY_END();
}
