/*
 * Bootloader — Plan 001 Phase 1.5 (skeleton) + 1.6 (verify) + 1.7 (A/B + fallback)
 *                 + 1.8 (OTA receiver entered via RTC backup register magic).
 *
 * Lives in sector 0 (0x08000000, 16 KB).  Boot flow:
 *
 *   0. Read RTC_BKP_DR0.  If it equals RTC_BACKUP_OTA_MAGIC, clear it
 *      and hand control to bootloader_ota_run(), which never returns —
 *      it either NVIC_SystemReset()s into the new image or halts.
 *
 *   1. Read both slot-metadata blobs (sectors 1 and 2).  Pick the active
 *      slot:
 *        - exactly one active + CRC valid     → that slot
 *        - both active                        → higher monotonic_counter
 *        - neither / both invalid             → default to A
 *   2. Verify the active slot's image (header parse + SHA-256 + ECDSA).
 *      On failure, fall back to the other slot and verify it.
 *   3. If both slots fail verify, halt with a distinct log line.
 *   4. Otherwise jump.
 *
 * verify_slot() is shared with the OTA receiver — Phase 1.8 reuses the
 * exact same SHA + ECDSA path so a freshly-flashed slot is held to the
 * same correctness bar as the boot path.
 */

#include <stdint.h>
#include <stddef.h>

#include "stm32f4xx.h"

#include "flash_slot.h"
#include "img_header.h"
#include "led2.h"
#include "ota.h"
#include "rtc_backup.h"
#include "uart.h"
#include "verify.h"

static const char *slot_name(flash_slot_id_t s)
{
    return (s == FLASH_SLOT_A) ? "A" : "B";
}

/* Slow blink + halt.  Visible failure mode for a stuck rig. */
static void __attribute__((noreturn)) bootloader_halt(void)
{
    led2_init();
    for (;;) {
        led2_toggle();
        for (volatile uint32_t i = 0; i < 2000000; ++i) {
            __asm volatile ("nop");
        }
    }
}

/*
 * Hand control to the app.  Tears down the bootloader's UART (which
 * armed USART2 RXNEIE + its NVIC entry), then masks interrupts only
 * across the VTOR/MSP change so a stray IRQ can't observe a half-
 * relocated state.  PRIMASK is cleared before the indirect call: the
 * app's Reset_Handler expects the same interrupt-enabled state the
 * chip has at hardware reset.
 */
static void __attribute__((noreturn)) jump_to_app(uint32_t app_base)
{
    uart_deinit();

    uint32_t app_msp   = *(volatile uint32_t *)(app_base);
    uint32_t app_reset = *(volatile uint32_t *)(app_base + 4U);

    __asm volatile ("cpsid i");
    SCB->VTOR = app_base;
    __asm volatile ("dsb");
    __asm volatile ("isb");
    __set_MSP(app_msp);
    __asm volatile ("cpsie i");
    ((void (*)(void))app_reset)();

    for (;;) { }
}

/*
 * Read the metadata blob for `slot`.  Returns 1 if the parse succeeded,
 * 0 otherwise.  An all-0xFF (erased) sector counts as "invalid" because
 * the CRC will never match.
 */
static int read_slot_metadata(flash_slot_id_t slot, img_slot_metadata_t *out)
{
    img_err_t rc = img_slot_metadata_parse(
        (const uint8_t *)flash_slot_metadata_address(slot),
        sizeof(img_slot_metadata_t),
        out);
    return (rc == IMG_OK) ? 1 : 0;
}

/*
 * Pick the active slot to verify first — same decision tree as Phase 1.7.
 */
static flash_slot_id_t pick_active_slot(int a_ok, const img_slot_metadata_t *a,
                                        int b_ok, const img_slot_metadata_t *b)
{
    if (a_ok && b_ok) {
        if (a->active && !b->active) return FLASH_SLOT_A;
        if (b->active && !a->active) return FLASH_SLOT_B;
        return (b->monotonic_counter > a->monotonic_counter) ? FLASH_SLOT_B
                                                             : FLASH_SLOT_A;
    }
    if (a_ok) return FLASH_SLOT_A;
    if (b_ok) return FLASH_SLOT_B;
    return FLASH_SLOT_A;
}

int main(void)
{
    uart_init();

    uart_puts("\r\nBL: stm32-bare-metal bootloader (Phase 1.8)\r\n");

    /*
     * Phase 1.8 hook — check the OTA magic before anything that
     * mutates the boot path. Backup-domain writes must be enabled
     * before we can clear the flag, so that comes first.
     */
    rtc_backup_enable_writes();
    if (rtc_backup_read_dr0() == RTC_BACKUP_OTA_MAGIC) {
        rtc_backup_write_dr0(0u);
        uart_puts("BL: OTA mode entered\r\n");
        bootloader_ota_run();
        /* If we get here, OTA halted on a verify or write failure. */
        bootloader_halt();
    }

    /* Read both metadata blobs.  All-FF (erased) sector → parse fails →
     * treat as "invalid" without halting; that's how a fresh chip with
     * no metadata yet still boots slot A. */
    img_slot_metadata_t md_a, md_b;
    int a_ok = read_slot_metadata(FLASH_SLOT_A, &md_a);
    int b_ok = read_slot_metadata(FLASH_SLOT_B, &md_b);

    uart_puts("BL: metadata A=");
    uart_puts(a_ok ? "ok" : "invalid");
    uart_puts(" B=");
    uart_puts(b_ok ? "ok" : "invalid");
    uart_puts("\r\n");

    flash_slot_id_t first  = pick_active_slot(a_ok, &md_a, b_ok, &md_b);
    flash_slot_id_t second = (first == FLASH_SLOT_A) ? FLASH_SLOT_B : FLASH_SLOT_A;

    uart_puts("BL: trying slot ");
    uart_puts(slot_name(first));
    uart_puts("\r\n");

    uint32_t app_base = 0u;
    uint32_t cycles   = 0u;

    flash_slot_id_t winner;
    verify_status_t st = verify_slot(first, &app_base, &cycles);
    if (st == VERIFY_OK) {
        winner = first;
    } else {
        uart_puts("BL: falling back to slot ");
        uart_puts(slot_name(second));
        uart_puts("\r\n");
        st = verify_slot(second, &app_base, &cycles);
        if (st == VERIFY_OK) {
            winner = second;
        } else {
            uart_puts("BL: both slots failed verify\r\n");
            bootloader_halt();
        }
    }

    uint32_t ms = cycles / 100000u;
    uart_puts("BL: verify ok slot=");
    uart_puts(slot_name(winner));
    uart_puts(" in ");
    uart_print_dec32(cycles);
    uart_puts(" cycles (~");
    uart_print_dec32(ms);
    uart_puts(" ms)\r\n");

    uart_puts("BL: jumping to slot ");
    uart_puts(slot_name(winner));
    uart_puts(" @ ");
    uart_print_hex32(app_base);
    uart_puts("\r\n");

    jump_to_app(app_base);
}
