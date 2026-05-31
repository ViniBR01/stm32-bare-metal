#include "flash_slot.h"

#include <string.h>

#include "flash.h"

/*
 * Sector lists per slot.  Keeping them static + const lets -O2 fold each
 * loop into a small straight-line erase-then-erase sequence.
 *
 * Slot A: sectors 4 (64 KB) + 5 (128 KB)            = 192 KB
 * Slot B: sector  6 (128 KB) + half of sector 7     = capped at 192 KB
 *   In practice we erase only sector 6 because sector 7 is reserved for
 *   future use (anti-rollback log, if Phase 1.9 pursues that route).
 *   The linker script caps slot-B builds at 128 KB through __slot_size_bytes
 *   so they cannot land inside sector 7.  See linker/app_ls.ld.
 */
static const uint8_t slot_a_sectors[] = { 4u, 5u };
static const uint8_t slot_b_sectors[] = { 6u };

#define SLOT_A_METADATA_SECTOR  1u
#define SLOT_B_METADATA_SECTOR  2u

err_t flash_slot_validate_range(uint32_t start, size_t len)
{
    if (len == 0u) {
        return ERR_INVALID_ARG;
    }

    /* Reject ranges that overflow uint32_t arithmetic. */
    uint32_t end_minus_one;
    if (__builtin_add_overflow(start, len - 1u, &end_minus_one)) {
        return ERR_INVALID_ARG;
    }

    /* Whole range must live in on-chip flash. */
    if (start < FLASH_BASE_ADDR || end_minus_one > FLASH_END_ADDR) {
        return ERR_INVALID_ARG;
    }

    /* Reject any overlap with the bootloader sector.  Sector 0 spans
     * 0x08000000..0x08003FFF (16 KB). */
    const uint32_t bl_start = FLASH_BASE_ADDR;
    const uint32_t bl_end   = FLASH_BASE_ADDR + (16u * 1024u) - 1u;
    if (!(end_minus_one < bl_start || start > bl_end)) {
        return ERR_INVALID_ARG;
    }

    return ERR_OK;
}

uint32_t flash_slot_base_address(flash_slot_id_t slot)
{
    switch (slot) {
    case FLASH_SLOT_A: return FLASH_SLOT_A_BASE;
    case FLASH_SLOT_B: return FLASH_SLOT_B_BASE;
    }
    return 0u;
}

uint32_t flash_slot_metadata_address(flash_slot_id_t slot)
{
    switch (slot) {
    case FLASH_SLOT_A: return FLASH_SLOT_A_METADATA;
    case FLASH_SLOT_B: return FLASH_SLOT_B_METADATA;
    }
    return 0u;
}

static err_t erase_sectors(const uint8_t *sectors, size_t count)
{
    err_t rc = flash_unlock();
    if (rc != ERR_OK) {
        return rc;
    }
    for (size_t i = 0; i < count; ++i) {
        rc = flash_erase_sector(sectors[i]);
        if (rc != ERR_OK) {
            flash_lock();
            return rc;
        }
    }
    flash_lock();
    return ERR_OK;
}

err_t flash_slot_erase(flash_slot_id_t slot)
{
    switch (slot) {
    case FLASH_SLOT_A:
        return erase_sectors(slot_a_sectors,
                             sizeof(slot_a_sectors) / sizeof(slot_a_sectors[0]));
    case FLASH_SLOT_B:
        return erase_sectors(slot_b_sectors,
                             sizeof(slot_b_sectors) / sizeof(slot_b_sectors[0]));
    }
    return ERR_INVALID_ARG;
}

err_t flash_slot_commit_metadata(flash_slot_id_t slot,
                                 const img_slot_metadata_t *md)
{
    if (md == NULL) {
        return ERR_INVALID_ARG;
    }

    uint8_t metadata_sector;
    switch (slot) {
    case FLASH_SLOT_A: metadata_sector = SLOT_A_METADATA_SECTOR; break;
    case FLASH_SLOT_B: metadata_sector = SLOT_B_METADATA_SECTOR; break;
    default:           return ERR_INVALID_ARG;
    }

    const uint32_t addr = flash_slot_metadata_address(slot);
    if (addr == 0u) {
        return ERR_INVALID_ARG;
    }

    /* Erase first.  A power-cut between erase and write leaves the
     * sector all-0xFF, which the parser rejects (CRC fails on FF). */
    err_t rc = flash_unlock();
    if (rc != ERR_OK) {
        return rc;
    }
    rc = flash_erase_sector(metadata_sector);
    if (rc != ERR_OK) {
        flash_lock();
        return rc;
    }

    rc = flash_write_bytes(addr,
                           (const uint8_t *)md,
                           sizeof(*md));
    flash_lock();
    if (rc != ERR_OK) {
        return rc;
    }

    /* Read-back verify.  Catches both flash-cell weakness and a
     * write-without-prior-erase (would leave 0-bits cleared from FF
     * to whatever, but stuck wrong-1-bits would survive). */
    uint8_t back[sizeof(*md)];
    rc = flash_read_bytes(addr, back, sizeof(back));
    if (rc != ERR_OK) {
        return rc;
    }
    if (memcmp(back, md, sizeof(back)) != 0) {
        return ERR_VERIFY;
    }
    return ERR_OK;
}
