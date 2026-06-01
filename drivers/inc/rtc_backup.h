#ifndef RTC_BACKUP_H
#define RTC_BACKUP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Minimal RTC backup-register driver.
 *
 * The STM32F411 has 20 backup registers (BKP0R..BKP19R) inside the RTC
 * peripheral. They survive a CPU reset and a brief power loss (powered
 * by VBAT or VDD), so they're the natural place to stash a "next-boot
 * intent" flag — exactly what Plan 001 Phase 1.8 needs to ask the
 * bootloader to enter OTA mode.
 *
 * Each register is independently addressable but writes require:
 *   1. RCC->APB1ENR.PWREN = 1   (clock the PWR controller)
 *   2. PWR->CR.DBP = 1          (disable backup-domain write protection)
 * Reads have no precondition.
 *
 * Backup-register usage map (Plan 001):
 *   BKP0R   — bootloader OTA entry magic. App writes RTC_BACKUP_OTA_MAGIC
 *             before NVIC_SystemReset(); bootloader reads + clears it.
 *   BKP1R..BKP19R — reserved for future use. See ota.md.
 */

#define RTC_BACKUP_OTA_MAGIC  0x4F544131u  /* "OTA1" little-endian */

/*
 * Enable write access to the backup domain. Idempotent: safe to call
 * multiple times. Required once per power-on before any backup-register
 * write; bootloader calls this at boot before reading + clearing BKP0R
 * so the clear-write succeeds.
 */
void rtc_backup_enable_writes(void);

/*
 * Read backup register 0 (BKP0R). No preconditions.
 */
uint32_t rtc_backup_read_dr0(void);

/*
 * Write backup register 0 (BKP0R). Caller must have invoked
 * rtc_backup_enable_writes() at least once since the last power-on
 * reset; calling this without DBP set silently no-ops in hardware.
 */
void rtc_backup_write_dr0(uint32_t value);

#ifdef __cplusplus
}
#endif

#endif /* RTC_BACKUP_H */
