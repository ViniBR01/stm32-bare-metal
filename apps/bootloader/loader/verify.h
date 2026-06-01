#ifndef BOOTLOADER_VERIFY_H
#define BOOTLOADER_VERIFY_H

#include <stdint.h>

#include "flash_slot.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bootloader image verification — extracted from main.c so the OTA
 * receiver (Plan 001 Phase 1.8) can reuse the same SHA-256 + ECDSA path
 * that normal boot does.
 */

typedef enum {
    VERIFY_OK            = 0,
    VERIFY_FAIL_PARSE    = 1,
    VERIFY_FAIL_TYPE     = 2,
    VERIFY_FAIL_SHA      = 3,
    VERIFY_FAIL_ECDSA    = 4,
} verify_status_t;

/*
 * Parse the header at the start of `slot` and verify the payload's
 * SHA-256 digest + ECDSA-P256 signature against the bootloader-embedded
 * public key. On success, *app_base_out is set to the absolute address
 * of the app vector table and *cycles_out receives the DWT cycle count
 * spent on SHA + verify (zero on failure).
 *
 * Logs every step over UART with a slot annotation so the HIL test can
 * grep the path.
 */
verify_status_t verify_slot(flash_slot_id_t slot,
                            uint32_t *app_base_out,
                            uint32_t *cycles_out);

#ifdef __cplusplus
}
#endif

#endif /* BOOTLOADER_VERIFY_H */
