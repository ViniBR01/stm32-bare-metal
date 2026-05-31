/*
 * Bootloader — Plan 001 Phase 1.5 (skeleton) + 1.6 (verify) + 1.7 (A/B + fallback).
 *
 * Lives in sector 0 (0x08000000, 16 KB).  Boot flow:
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
 * Phase 1.6's verify call is invoked unchanged from a small wrapper —
 * Phase 1.7 only adds the slot-pick + retry around it.  fail_count and
 * monotonic_counter live in the metadata struct but the bootloader does
 * not yet write them; that lands with rollback-on-crash semantics in a
 * later phase together with Phase 1.9 anti-rollback.
 */

#include <stdint.h>
#include <stddef.h>

#include "stm32f4xx.h"

#include "crypto.h"
#include "flash_slot.h"
#include "img_header.h"
#include "led2.h"
#include "uart.h"

extern const uint8_t bootloader_pubkey[CRYPTO_ECDSA_P256_PUBKEY_LEN];

/* Fixed-buffer console — no printf, no DMA: keeps sector-0 footprint tiny. */
static void uart_print(const char *s)
{
    while (*s) {
        uart_write(*s++);
    }
}

static void uart_print_hex32(uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[11];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 8; ++i) {
        buf[2 + i] = hex[(v >> ((7 - i) * 4)) & 0xFu];
    }
    buf[10] = '\0';
    uart_print(buf);
}

static void uart_print_dec32(uint32_t v)
{
    char buf[11];
    int  i = (int)sizeof(buf) - 1;
    buf[i--] = '\0';
    if (v == 0) {
        buf[i--] = '0';
    } else {
        while (v != 0 && i >= 0) {
            buf[i--] = (char)('0' + (v % 10u));
            v /= 10u;
        }
    }
    uart_print(&buf[i + 1]);
}

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
 * Read the metadata blob for `slot`.  Returns 1 if the parse succeeded
 * (the buffer was a valid img_slot_metadata_t with matching magic, CRC,
 * version), 0 otherwise.  An all-0xFF (erased) sector counts as
 * "invalid" because the CRC will never match.
 */
/*
 * The bootloader uses just the slot-base / metadata-address constants
 * from lib/flash, not any of its mutation code, so we resolve them
 * inline rather than linking libflash.a into sector 0.
 */
static uint32_t slot_base_inline(flash_slot_id_t s)
{
    return (s == FLASH_SLOT_A) ? FLASH_SLOT_A_BASE : FLASH_SLOT_B_BASE;
}

static uint32_t slot_metadata_inline(flash_slot_id_t s)
{
    return (s == FLASH_SLOT_A) ? FLASH_SLOT_A_METADATA : FLASH_SLOT_B_METADATA;
}

static int read_slot_metadata(flash_slot_id_t slot, img_slot_metadata_t *out)
{
    img_err_t rc = img_slot_metadata_parse((const uint8_t *)slot_metadata_inline(slot),
                                           sizeof(img_slot_metadata_t),
                                           out);
    return (rc == IMG_OK) ? 1 : 0;
}

/*
 * Pick the active slot to verify first.
 *
 *   - If only one metadata blob is valid AND it has active != 0    → that slot
 *   - If both valid AND both active                                → higher
 *                                                                    monotonic_counter
 *                                                                    wins (ties → A)
 *   - If only one valid (active or not)                            → that slot
 *   - If neither valid                                             → A (skeleton-
 *                                                                    compatible
 *                                                                    default)
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

/*
 * Run header parse + SHA-256 + ECDSA verify on `slot`.  On success, sets
 * *app_base_out to the absolute address of the app vector table and
 * returns IMG_OK.  Logs every step with a slot annotation so HIL can
 * grep the path.  Returns the same img_err_t used by Phase 1.6 plus
 * IMG_OK on full success; signature-rejection is mapped to a non-OK
 * value (we reuse IMG_ERR_BAD_CRC since the failure means "this image
 * cannot be trusted" and the parser already exhausts the existing
 * codes).
 */
typedef enum {
    VERIFY_OK              = 0,
    VERIFY_FAIL_PARSE      = 1,
    VERIFY_FAIL_TYPE       = 2,
    VERIFY_FAIL_SHA        = 3,
    VERIFY_FAIL_ECDSA      = 4,
} verify_status_t;

static verify_status_t verify_slot(flash_slot_id_t slot, uint32_t *app_base_out,
                                   uint32_t *cycles_out)
{
    const uint32_t slot_base = slot_base_inline(slot);

    img_header_t hdr;
    img_err_t rc = img_header_parse((const uint8_t *)slot_base,
                                    sizeof(img_header_t), &hdr);
    if (rc != IMG_OK) {
        uart_print("BL: slot ");
        uart_print(slot_name(slot));
        uart_print(" header parse failed: rc=");
        uart_print_hex32((uint32_t)rc);
        uart_print("\r\n");
        return VERIFY_FAIL_PARSE;
    }

    if (hdr.image_type != IMG_TYPE_APP) {
        uart_print("BL: slot ");
        uart_print(slot_name(slot));
        uart_print(" image_type != APP\r\n");
        return VERIFY_FAIL_TYPE;
    }

    const uint8_t *payload = (const uint8_t *)(slot_base + hdr.payload_offset);

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

    uint8_t computed[CRYPTO_SHA256_DIGEST_LEN];
    crypto_sha256(payload, hdr.payload_size, computed);

    if (crypto_memcmp_ct(computed, hdr.sha256, CRYPTO_SHA256_DIGEST_LEN) != 0) {
        uart_print("BL: slot ");
        uart_print(slot_name(slot));
        uart_print(" verify FAILED: sha mismatch\r\n");
        return VERIFY_FAIL_SHA;
    }

    if (crypto_ecdsa_p256_verify(bootloader_pubkey, computed, hdr.signature) != 1) {
        uart_print("BL: slot ");
        uart_print(slot_name(slot));
        uart_print(" verify FAILED: ecdsa reject\r\n");
        return VERIFY_FAIL_ECDSA;
    }

    *cycles_out = DWT->CYCCNT;
    *app_base_out = slot_base + hdr.payload_offset;
    return VERIFY_OK;
}

int main(void)
{
    uart_init();

    uart_print("\r\nBL: stm32-bare-metal bootloader (Phase 1.7)\r\n");

    /* Read both metadata blobs.  All-FF (erased) sector → parse fails →
     * treat as "invalid" without halting; that's how a fresh chip with
     * no metadata yet still boots slot A. */
    img_slot_metadata_t md_a, md_b;
    int a_ok = read_slot_metadata(FLASH_SLOT_A, &md_a);
    int b_ok = read_slot_metadata(FLASH_SLOT_B, &md_b);

    uart_print("BL: metadata A=");
    uart_print(a_ok ? "ok" : "invalid");
    uart_print(" B=");
    uart_print(b_ok ? "ok" : "invalid");
    uart_print("\r\n");

    flash_slot_id_t first  = pick_active_slot(a_ok, &md_a, b_ok, &md_b);
    flash_slot_id_t second = (first == FLASH_SLOT_A) ? FLASH_SLOT_B : FLASH_SLOT_A;

    uart_print("BL: trying slot ");
    uart_print(slot_name(first));
    uart_print("\r\n");

    uint32_t app_base = 0u;
    uint32_t cycles   = 0u;

    flash_slot_id_t winner;
    verify_status_t st = verify_slot(first, &app_base, &cycles);
    if (st == VERIFY_OK) {
        winner = first;
    } else {
        uart_print("BL: falling back to slot ");
        uart_print(slot_name(second));
        uart_print("\r\n");
        st = verify_slot(second, &app_base, &cycles);
        if (st == VERIFY_OK) {
            winner = second;
        } else {
            uart_print("BL: both slots failed verify\r\n");
            bootloader_halt();
        }
    }

    uint32_t ms = cycles / 100000u;
    uart_print("BL: verify ok slot=");
    uart_print(slot_name(winner));
    uart_print(" in ");
    uart_print_dec32(cycles);
    uart_print(" cycles (~");
    uart_print_dec32(ms);
    uart_print(" ms)\r\n");

    uart_print("BL: jumping to slot ");
    uart_print(slot_name(winner));
    uart_print(" @ ");
    uart_print_hex32(app_base);
    uart_print("\r\n");

    jump_to_app(app_base);
}
