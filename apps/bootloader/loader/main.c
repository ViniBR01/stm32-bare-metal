/*
 * Bootloader — Plan 001 Phase 1.5 (skeleton) + 1.6 (verify) + 1.7 (A/B + fallback)
 *                 + 1.8 (OTA receiver entered via RTC backup register magic)
 *                 + 1.9 (anti-rollback floor + fail_count writes).
 *
 * Lives in sector 0 (0x08000000, 16 KB).  Boot flow:
 *
 *   0. Read RTC_BKP_DR0.  If it equals RTC_BACKUP_OTA_MAGIC, clear it
 *      and hand control to bootloader_ota_run(), which never returns —
 *      it either NVIC_SystemReset()s into the new image or halts.
 *
 *   1. Read both slot-metadata blobs (sectors 1 and 2).  Compute the
 *      anti-rollback floor as max(slot_a.monotonic_counter,
 *      slot_b.monotonic_counter).  Pick the active slot:
 *        - exactly one active + CRC valid     → that slot
 *        - both active                        → higher monotonic_counter
 *        - neither / both invalid             → default to A
 *   2. fail_count gate:  if the picked slot's metadata reports
 *      fail_count >= IMG_FAIL_COUNT_MAX, treat that slot as failed and
 *      fall back to the other slot — same path as a verify-fail.  This
 *      is the rollback-on-crash semantic: the bootloader bumps
 *      fail_count *before* the jump (step 5), and the app clears it
 *      after init via bl_handshake_clear_fail_count().  If the app
 *      crashes before it can clear, fail_count walks toward the cap.
 *   3. Verify the chosen slot (header parse + SHA-256 + ECDSA).  On
 *      verify failure or rollback failure, fall back to the other slot.
 *   4. Floor gate:  the freshly-verified header's image_version must be
 *      >= floor or the slot is treated as failed (rollback rejected).
 *      Same fallback path as above.
 *   5. Single metadata write before jump: increment fail_count, advance
 *      monotonic_counter to max(prev counter, image_version, floor),
 *      mark active=1.  All three updates are one erase + program +
 *      readback inside flash_slot_commit_metadata.
 *   6. Jump.
 *
 * verify_slot() is shared with the OTA receiver — Phase 1.8 reuses the
 * exact same SHA + ECDSA path so a freshly-flashed slot is held to the
 * same correctness bar as the boot path.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "stm32f4xx.h"

#include "error.h"
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
 * Per-attempt state — kept on the stack so a fall-back attempt does
 * not see leftovers from the first attempt.
 */
typedef struct {
    flash_slot_id_t      slot;
    img_header_t         header;
    uint32_t             app_base;
    uint32_t             cycles;
    int                  md_was_valid;
    img_slot_metadata_t  prev_md;     /* zero-init when !md_was_valid */
} boot_attempt_t;

/*
 * Try one slot end-to-end: fail_count gate → verify → floor gate.
 * Returns 1 on success (and fills *out), 0 otherwise.  All failure
 * branches log their own line so the HIL test can grep them.
 */
static int try_boot_slot(flash_slot_id_t slot, uint32_t floor,
                         int md_valid, const img_slot_metadata_t *md,
                         boot_attempt_t *out)
{
    memset(out, 0, sizeof(*out));
    out->slot         = slot;
    out->md_was_valid = md_valid;
    if (md_valid) {
        out->prev_md = *md;
    }

    if (md_valid && img_fail_count_tripped(md->fail_count)) {
        uart_puts("BL: fc tripped\r\n");
        return 0;
    }

    verify_status_t st = verify_slot(slot, &out->app_base, &out->cycles,
                                     &out->header);
    if (st != VERIFY_OK) {
        return 0;
    }

    if (!img_header_meets_floor(&out->header, floor)) {
        /* Format is load-bearing for the HIL test. */
        uart_puts("BL: rollback ver=");
        uart_print_dec32(out->header.image_version);
        uart_puts(" < floor=");
        uart_print_dec32(floor);
        uart_puts("\r\n");
        return 0;
    }

    return 1;
}

/*
 * Build the post-boot metadata blob for the winning slot and commit it
 * to flash.  Single metadata commit covers fail_count++, monotonic
 * advance to max(prev counter, image_version), and active=1.
 *
 * Failure here is logged but does NOT block the jump.  The verified
 * image runs even if the metadata write fails — losing the
 * rollback-on-crash signal for that boot is preferable to refusing to
 * run a known-good image.  The next clean boot will re-attempt the
 * commit.
 */
static void commit_post_boot_metadata(const boot_attempt_t *att, uint32_t floor)
{
    img_slot_metadata_t md;
    if (att->md_was_valid) {
        md = att->prev_md;
    } else {
        memset(&md, 0, sizeof(md));
    }

    md.active     = 1u;
    md.fail_count = img_fail_count_increment(att->md_was_valid
                                             ? att->prev_md.fail_count
                                             : 0u);
    /* New counter dominates: the verified image_version, the existing
     * floor, and (if md was valid) the slot's own previous counter.
     * That keeps the floor monotonic across boots even if a slot's
     * counter was lower than the cross-slot max. */
    uint32_t new_counter = att->header.image_version;
    if (floor > new_counter)            new_counter = floor;
    if (att->md_was_valid && att->prev_md.monotonic_counter > new_counter) {
        new_counter = att->prev_md.monotonic_counter;
    }
    md.monotonic_counter = new_counter;

    img_slot_metadata_finalize(&md);

    /* commit failure is logged inline so a flash-error doesn't go silent. */
    if (flash_slot_commit_metadata(att->slot, &md) != ERR_OK) {
        uart_puts("BL: md commit FAIL\r\n");
    }
}

int main(void)
{
    uart_init();

    uart_puts("\r\nBL: stm32-bare-metal bootloader (Phase 1.9)\r\n");

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

    uart_puts("BL: md A=");
    uart_puts(a_ok ? "ok" : "X");
    uart_puts(" B=");
    uart_puts(b_ok ? "ok" : "X");
    uart_puts("\r\n");

    uint32_t floor = img_compute_floor(a_ok, &md_a, b_ok, &md_b);

    flash_slot_id_t first  =
        (flash_slot_id_t)img_pick_active_slot(a_ok, &md_a, b_ok, &md_b);
    flash_slot_id_t second = (first == FLASH_SLOT_A) ? FLASH_SLOT_B : FLASH_SLOT_A;

    int first_md_ok  = (first  == FLASH_SLOT_A) ? a_ok : b_ok;
    const img_slot_metadata_t *first_md  = (first  == FLASH_SLOT_A) ? &md_a : &md_b;
    int second_md_ok = (second == FLASH_SLOT_A) ? a_ok : b_ok;
    const img_slot_metadata_t *second_md = (second == FLASH_SLOT_A) ? &md_a : &md_b;

    uart_puts("BL: trying slot ");
    uart_puts(slot_name(first));
    uart_puts("\r\n");

    boot_attempt_t att;
    int ok = try_boot_slot(first, floor, first_md_ok, first_md, &att);
    if (!ok) {
        uart_puts("BL: falling back to slot ");
        uart_puts(slot_name(second));
        uart_puts("\r\n");
        ok = try_boot_slot(second, floor, second_md_ok, second_md, &att);
        if (!ok) {
            uart_puts("BL: both slots failed verify\r\n");
            bootloader_halt();
        }
    }

    uint32_t ms = att.cycles / 100000u;
    uart_puts("BL: verify ok slot=");
    uart_puts(slot_name(att.slot));
    uart_puts(" in ");
    uart_print_dec32(att.cycles);
    uart_puts(" cycles (~");
    uart_print_dec32(ms);
    uart_puts(" ms)\r\n");

    commit_post_boot_metadata(&att, floor);

    uart_puts("BL: jumping to slot ");
    uart_puts(slot_name(att.slot));
    uart_puts(" @ ");
    uart_print_hex32(att.app_base);
    uart_puts("\r\n");

    jump_to_app(att.app_base);
}
