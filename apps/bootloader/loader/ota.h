#ifndef BOOTLOADER_OTA_H
#define BOOTLOADER_OTA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Plan 001 Phase 1.8 — bootloader OTA receiver.
 *
 * Entered when the bootloader sees the OTA magic in RTC_BKP_DR0 at startup.
 * Drives a blocking UART → framing decoder → OTA state machine on USART2,
 * writes the inactive slot, runs the same verify path the normal boot uses,
 * and atomically flips the active flag in metadata on success.
 *
 * This function never returns — it either reboots the chip into the new
 * image (NVIC_SystemReset) or halts after reporting a failure status.
 */
void bootloader_ota_run(void);

/* Status byte sent back on the wire as the body of FRAME_TYPE_STATUS. */
typedef enum {
    OTA_STATUS_OK                = 0,
    OTA_STATUS_VERIFY_FAILED     = 1,
    OTA_STATUS_WRITE_FAILED      = 2,
    OTA_STATUS_PROTOCOL_ERROR    = 3,
    /* Phase 1.9: image survived SHA+ECDSA but its image_version was
     * lower than the rollback floor (max monotonic_counter seen on
     * either slot).  Bytes were written but no metadata was
     * committed — the previously-active slot is still active. */
    OTA_STATUS_ROLLBACK_REJECTED = 4,
} ota_status_t;

#ifdef __cplusplus
}
#endif

#endif /* BOOTLOADER_OTA_H */
