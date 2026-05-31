#include "flash_slot.h"
#include "unity.h"

#include <stdint.h>

/*
 * Host tests for the pure-logic surface of lib/flash:
 *
 *   - flash_slot_validate_range()   — bootloader-sector overlap detection
 *   - flash_slot_base_address()     — slot id → flash address
 *   - flash_slot_metadata_address() — slot id → metadata sector address
 *
 * The mutating primitives (flash_slot_erase, flash_slot_commit_metadata)
 * are exercised end-to-end on real hardware by scripts/run_ab_slot_test.py.
 * The fake flash buffer in drivers/src/flash.c covers only the first
 * 16 KB sector, which isn't enough to host-test slot erases that span
 * sectors 4–6 (192 KB).
 */

void setUp(void) {}
void tearDown(void) {}

/* ---------- flash_slot_base_address ---------- */

void test_base_addr_slot_a(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x08010000u, flash_slot_base_address(FLASH_SLOT_A));
}

void test_base_addr_slot_b(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x08040000u, flash_slot_base_address(FLASH_SLOT_B));
}

void test_base_addr_invalid_returns_zero(void)
{
    TEST_ASSERT_EQUAL_HEX32(0u, flash_slot_base_address((flash_slot_id_t)99));
}

/* ---------- flash_slot_metadata_address ---------- */

void test_metadata_addr_slot_a(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x08004000u, flash_slot_metadata_address(FLASH_SLOT_A));
}

void test_metadata_addr_slot_b(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x08008000u, flash_slot_metadata_address(FLASH_SLOT_B));
}

void test_metadata_addr_invalid_returns_zero(void)
{
    TEST_ASSERT_EQUAL_HEX32(0u, flash_slot_metadata_address((flash_slot_id_t)42));
}

/* ---------- flash_slot_validate_range ---------- */

void test_validate_range_zero_length_rejected(void)
{
    TEST_ASSERT_EQUAL_INT(ERR_INVALID_ARG,
                          flash_slot_validate_range(0x08010000u, 0u));
}

void test_validate_range_below_flash_rejected(void)
{
    TEST_ASSERT_EQUAL_INT(ERR_INVALID_ARG,
                          flash_slot_validate_range(0x07FFFFFFu, 16u));
}

void test_validate_range_past_flash_end_rejected(void)
{
    /* Last byte just past sector 7 end (0x0807FFFF). */
    TEST_ASSERT_EQUAL_INT(ERR_INVALID_ARG,
                          flash_slot_validate_range(0x0807FFFFu, 2u));
}

void test_validate_range_inside_bootloader_rejected(void)
{
    /* Anywhere inside sector 0 (0x08000000..0x08003FFF) must be refused. */
    TEST_ASSERT_EQUAL_INT(ERR_INVALID_ARG,
                          flash_slot_validate_range(0x08000000u, 4u));
    TEST_ASSERT_EQUAL_INT(ERR_INVALID_ARG,
                          flash_slot_validate_range(0x08002000u, 1u));
    TEST_ASSERT_EQUAL_INT(ERR_INVALID_ARG,
                          flash_slot_validate_range(0x08003FFCu, 4u));
}

void test_validate_range_straddling_bootloader_boundary_rejected(void)
{
    /* Range that starts just before sector 0's end and crosses into sector 1
     * still touches the bootloader sector and must be refused. */
    TEST_ASSERT_EQUAL_INT(ERR_INVALID_ARG,
                          flash_slot_validate_range(0x08003FFCu, 16u));
}

void test_validate_range_slot_a_metadata_accepted(void)
{
    /* Sector 1 (16 KB) — the slot-A metadata location. */
    TEST_ASSERT_EQUAL_INT(ERR_OK,
                          flash_slot_validate_range(0x08004000u, 36u));
}

void test_validate_range_slot_a_payload_accepted(void)
{
    TEST_ASSERT_EQUAL_INT(ERR_OK,
                          flash_slot_validate_range(0x08010000u, 192u * 1024u));
}

void test_validate_range_slot_b_payload_accepted(void)
{
    TEST_ASSERT_EQUAL_INT(ERR_OK,
                          flash_slot_validate_range(0x08040000u, 128u * 1024u));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_base_addr_slot_a);
    RUN_TEST(test_base_addr_slot_b);
    RUN_TEST(test_base_addr_invalid_returns_zero);

    RUN_TEST(test_metadata_addr_slot_a);
    RUN_TEST(test_metadata_addr_slot_b);
    RUN_TEST(test_metadata_addr_invalid_returns_zero);

    RUN_TEST(test_validate_range_zero_length_rejected);
    RUN_TEST(test_validate_range_below_flash_rejected);
    RUN_TEST(test_validate_range_past_flash_end_rejected);
    RUN_TEST(test_validate_range_inside_bootloader_rejected);
    RUN_TEST(test_validate_range_straddling_bootloader_boundary_rejected);
    RUN_TEST(test_validate_range_slot_a_metadata_accepted);
    RUN_TEST(test_validate_range_slot_a_payload_accepted);
    RUN_TEST(test_validate_range_slot_b_payload_accepted);

    return UNITY_END();
}
