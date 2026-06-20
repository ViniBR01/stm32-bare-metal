#include "bl_handshake.h"

#include <stdint.h>

#include "flash_slot.h"
#include "img_header.h"

err_t bl_handshake_clear_fail_count(flash_slot_id_t slot)
{
    img_slot_metadata_t md;
    img_err_t prc = img_slot_metadata_parse(
        (const uint8_t *)flash_slot_metadata_address(slot),
        sizeof(md), &md);
    if (prc != IMG_OK) {
        /* Pristine sector or corrupt metadata — nothing to clear, but
         * surface the parse failure so the caller can distinguish it
         * from a successful clear.  ERR_VERIFY is the closest existing
         * code; bl_handshake.h documents the contract. */
        return ERR_VERIFY;
    }
    if (md.fail_count == 0u) {
        /* Already clean — no flash wear, no commit. */
        return ERR_OK;
    }
    md.fail_count = 0u;
    img_slot_metadata_finalize(&md);
    return flash_slot_commit_metadata(slot, &md);
}
