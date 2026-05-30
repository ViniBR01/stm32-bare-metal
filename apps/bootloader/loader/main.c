/*
 * Bootloader skeleton — Plan 001 Phase 1.5.
 *
 * Lives in sector 0 (0x08000000, 16 KB).  Reads the img_header_t at the
 * start of slot A, sanity-checks magic + CRC via lib/img, and jumps to the
 * app reset vector if the header parses cleanly.  Signature verification is
 * intentionally skipped at this stage — it is wired up in Phase 1.6 against
 * the bootloader_pubkey already linked into this image.
 */

#include <stdint.h>
#include <stddef.h>

#include "stm32f4xx.h"

#include "img_header.h"
#include "led2.h"
#include "uart.h"

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
 * Hand control to the app.  Mask interrupts only across the VTOR/MSP
 * change (so a stray IRQ can't observe a half-relocated state), then
 * unmask before transferring control — the app's Reset_Handler expects
 * the same PRIMASK state the chip has at hardware reset (interrupts
 * enabled).
 */
static void __attribute__((noreturn)) jump_to_app(uint32_t app_base)
{
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

    uart_print("\r\nBL: stm32-bare-metal bootloader (Phase 1.5)\r\n");

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

    uint32_t app_base = SLOT_A_BASE + hdr.payload_offset;
    uart_print("BL: jumping to slot A @ ");
    uart_print_hex32(app_base);
    uart_print("\r\n");

    jump_to_app(app_base);
}
