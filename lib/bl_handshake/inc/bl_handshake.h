#ifndef LIB_BL_HANDSHAKE_H
#define LIB_BL_HANDSHAKE_H

/*
 * Plan 001 Phase 1.9 — bootloader/app handshake.
 *
 * The bootloader increments a slot's `fail_count` *before* jumping into
 * the app.  A clean boot is signalled by the app calling
 * bl_handshake_clear_fail_count() early in main() — typically right
 * after the chip's basic init has succeeded.  When the bootloader sees
 * `fail_count >= IMG_FAIL_COUNT_MAX` on the active slot it treats the
 * slot as failed and falls back to the other slot, the same way it
 * handles a verify failure.
 *
 * Recovery from a "fail_count storm" (a buggy app that resets between
 * init and the clear call) is the operator-forced OTA path documented
 * in docs/wiki/plans/001-bootloader/ota.md — write the OTA magic via
 * OpenOCD, reset, and stream a fixed image.
 *
 * This helper does the metadata read-modify-write itself so callers
 * don't have to know about lib/img or lib/flash.  It is safe to call
 * any number of times — if the slot's fail_count is already 0 the
 * commit is skipped (no flash wear).
 */

#include <stdint.h>

#include "error.h"
#include "flash_slot.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Reset the active slot's fail_count to 0 and commit the metadata.
 *
 *   slot         : the slot the bootloader jumped from.  Apps can
 *                  hard-code this if they only ship in one slot, but
 *                  the more correct path is to read it from a build-
 *                  time symbol or from the linker-published
 *                  _app_vector_base address.
 *
 * Returns:
 *   ERR_OK         on success or when the metadata was already clean
 *                  (fail_count == 0).
 *   ERR_VERIFY     if the slot's metadata sector cannot be parsed
 *                  (pristine chip with all-FF metadata, or a corrupt
 *                  sector).  The bootloader will seed/repair the
 *                  metadata on its next pass — no app action needed.
 *   ERR_INVALID_ARG, ERR_TIMEOUT, ERR_BUSY
 *                  flash erase / program / readback error bubbled up
 *                  from flash_slot_commit_metadata().
 */
err_t bl_handshake_clear_fail_count(flash_slot_id_t slot);

#ifdef __cplusplus
}
#endif

#endif /* LIB_BL_HANDSHAKE_H */
