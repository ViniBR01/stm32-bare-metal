/*
 * Bootloader — Plan 001 Phase 1.5 (skeleton) + Phase 1.6 (verify-and-jump).
 *
 * Lives in sector 0 (0x08000000, 16 KB).  After parsing the slot-A
 * img_header_t (lib/img), Phase 1.6 computes SHA-256 over the payload,
 * compares it constant-time against hdr.sha256, then verifies an
 * ECDSA-P256 signature against the bootloader_pubkey baked into this
 * image at link time.  Verify time is captured with the DWT cycle counter
 * and logged on success.  Any failure halts; slot-B fallback is Phase 1.7.
 */

#include <stdint.h>
#include <stddef.h>

#include "stm32f4xx.h"

#include "crypto.h"
#include "img_header.h"
#include "led2.h"
#include "uart.h"

/*
 * Public key emitted by tools/keygen.py into $(BL_PUBKEY_C) and compiled
 * into this image.  The matching private key signs every app the bootloader
 * is asked to verify.  See Makefile.common's `keys` target.
 */
extern const uint8_t bootloader_pubkey[CRYPTO_ECDSA_P256_PUBKEY_LEN];

/*
 * Slot A starts at sector 4 (0x08010000).  The signed image lives there:
 * 140-byte img_header_t followed by the raw payload (vector table + code).
 */
#define SLOT_A_BASE  0x08010000u

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

/* Decimal print for a uint32_t — used by the verify-time log line. */
static void uart_print_dec32(uint32_t v)
{
    char buf[11];  /* 4294967295 fits in 10 digits + NUL */
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

    /* Unreachable. */
    for (;;) { }
}

int main(void)
{
    /* SystemInit (called from Reset_Handler before main) already brought
     * the system clock up to 100 MHz from HSI via PLL.  Calling rcc_init
     * a second time would attempt to clear PLLON while PLL is the active
     * sysclk source — the chip stalls in that combination — so we just
     * bring up USART2 here. */
    uart_init();

    uart_print("\r\nBL: stm32-bare-metal bootloader (Phase 1.6)\r\n");

    img_header_t hdr;
    img_err_t rc = img_header_parse((const uint8_t *)SLOT_A_BASE,
                                    sizeof(img_header_t), &hdr);
    if (rc != IMG_OK) {
        uart_print("BL: slot A header parse failed: rc=");
        uart_print_hex32((uint32_t)rc);
        uart_print("\r\n");
        bootloader_halt();
    }

    if (hdr.image_type != IMG_TYPE_APP) {
        uart_print("BL: slot A image_type != APP\r\n");
        bootloader_halt();
    }

    /*
     * Phase 1.6 — verify the signed payload before jumping.
     *
     * 1. Compute SHA-256 over the payload region in flash.
     * 2. Constant-time compare against hdr.sha256 (catches plain bit-flips
     *    and short-circuits the expensive ECDSA path on a clearly tampered
     *    image, while still hitting verify even for header-clean tampers).
     * 3. ECDSA-P256 verify the signature against the computed digest.
     *
     * DWT->CYCCNT brackets the whole verify so the log captures the full
     * cost, not just the ECDSA step.  Hard cap is 500 ms per Plan 001 §1.3.
     */
    const uint8_t *payload = (const uint8_t *)(SLOT_A_BASE + hdr.payload_offset);

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

    uint8_t computed[CRYPTO_SHA256_DIGEST_LEN];
    crypto_sha256(payload, hdr.payload_size, computed);

    if (crypto_memcmp_ct(computed, hdr.sha256, CRYPTO_SHA256_DIGEST_LEN) != 0) {
        uart_print("BL: verify FAILED: sha mismatch\r\n");
        bootloader_halt();
    }

    if (crypto_ecdsa_p256_verify(bootloader_pubkey, computed, hdr.signature) != 1) {
        uart_print("BL: verify FAILED: ecdsa reject\r\n");
        bootloader_halt();
    }

    uint32_t cycles = DWT->CYCCNT;
    /* SYSCLK is 100 MHz; 100 000 cycles == 1 ms.  Verify completes in tens
     * of millions of cycles, so neither this division nor the uint32_t
     * format width can overflow for any realistic payload. */
    uint32_t ms = cycles / 100000u;

    uart_print("BL: verify ok in ");
    uart_print_dec32(cycles);
    uart_print(" cycles (~");
    uart_print_dec32(ms);
    uart_print(" ms)\r\n");

    uint32_t app_base = SLOT_A_BASE + hdr.payload_offset;
    uart_print("BL: jumping to slot A @ ");
    uart_print_hex32(app_base);
    uart_print("\r\n");

    jump_to_app(app_base);
}
