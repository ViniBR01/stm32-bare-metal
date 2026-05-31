/*
 * lib/flash — slot-aware flash middleware (Plan 001 Phase 1.7).
 *
 * Wraps the low-level drivers/src/flash.c API with three protections that
 * keep bootloader/slot operations safe:
 *
 *  - flash_slot_validate_range(): refuses any range that overlaps the
 *    bootloader sector (sector 0).  Kept as a separate predicate so the
 *    bootloader can call it before invoking the lower-level erase/write
 *    primitives directly.
 *
 *  - flash_slot_erase(): erases the sector range backing a slot.  Knows
 *    the SLOT_A / SLOT_B sector maps so callers cannot accidentally
 *    pass in a number that belongs to the wrong slot or to the
 *    metadata sector.
 *
 *  - flash_slot_commit_metadata(): writes a complete img_slot_metadata_t
 *    into a freshly-erased metadata sector and reads it back to confirm
 *    the bytes landed.  Power-cut safety: the slot metadata sector is
 *    erased first, then written; an interruption between erase and write
 *    leaves the sector blank, which the bootloader treats as "metadata
 *    invalid" and falls back accordingly.
 *
 * No dynamic allocation.  Compiles for both host (UNIT_TEST) and
 * arm-none-eabi targets.  Bootloader links libflash.a only when slot
 * mutation is needed; the verify-and-jump path stays read-only.
 */
#ifndef LIB_FLASH_SLOT_H
#define LIB_FLASH_SLOT_H

#include <stdint.h>
#include <stddef.h>

#include "error.h"
#include "img_header.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Slot identifiers.  The numeric values double as array indices in
 * partition_dump.py output and in any future slot-history code. */
typedef enum {
    FLASH_SLOT_A = 0,
    FLASH_SLOT_B = 1,
} flash_slot_id_t;

/* On-flash addresses.  These constants are the single source of truth
 * for the partition layout — the linker scripts, sign_image.py, and the
 * bootloader all derive their values from these. */
#define FLASH_SLOT_A_BASE         0x08010000u  /* Sector 4         */
#define FLASH_SLOT_B_BASE         0x08040000u  /* Sector 6         */
#define FLASH_SLOT_SIZE           (192u * 1024u)
#define FLASH_SLOT_A_METADATA     0x08004000u  /* Sector 1, 16 KB  */
#define FLASH_SLOT_B_METADATA     0x08008000u  /* Sector 2, 16 KB  */

#define FLASH_BOOTLOADER_SECTOR   0u

/*
 * Validate that a flash range is safe to erase or program.  Returns
 * ERR_OK if [start, start + len) is entirely within on-chip flash AND
 * does not overlap the bootloader sector.  Returns ERR_INVALID_ARG
 * otherwise.
 */
err_t flash_slot_validate_range(uint32_t start, size_t len);

/*
 * Erase every sector backing the given slot.  For both A and B that's a
 * 192 KB region.  Internally calls flash_unlock(), iterates the sector
 * list, calls flash_lock() on exit (or on error).  Slow — ~hundreds of
 * milliseconds — but only invoked from OTA code paths.
 */
err_t flash_slot_erase(flash_slot_id_t slot);

/*
 * Erase the metadata sector for `slot` and write `md` into it at the
 * sector base.  The metadata struct's CRC must already be populated by
 * the caller; this function does NOT recompute it.  After programming,
 * the bytes are read back and compared to `md`; ERR_VERIFY is returned
 * on mismatch.
 *
 * Power-cut safety: the metadata sector is erased before the write, so
 * an interruption between the two steps leaves the sector all-0xFF —
 * the parser will reject it (CRC mismatch on an FF sector) and the
 * bootloader will fall back to the other slot.  No partial-write
 * window.
 */
err_t flash_slot_commit_metadata(flash_slot_id_t slot,
                                 const img_slot_metadata_t *md);

/*
 * Returns the absolute flash address of the slot's payload region (i.e.
 * the address where the signed image's img_header_t would be flashed).
 * 0 if `slot` is invalid.
 */
uint32_t flash_slot_base_address(flash_slot_id_t slot);

/*
 * Returns the absolute flash address of the slot's metadata sector.
 * 0 if `slot` is invalid.
 */
uint32_t flash_slot_metadata_address(flash_slot_id_t slot);

#ifdef __cplusplus
}
#endif

#endif /* LIB_FLASH_SLOT_H */
