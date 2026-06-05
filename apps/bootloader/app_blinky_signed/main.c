/*
 * app_blinky_signed — Plan 001 Phase 1.5 smoke-test app.
 *
 * Trivial slot-A app linked at SLOT_BASE + sizeof(img_header_t).  The
 * bootloader jumps here after parsing the header.  Emits one boot-log line
 * over USART2 so the HIL boot smoke test can assert the boot chain works
 * end-to-end, then toggles LED2 forever.
 *
 * Phase 1.9: calls bl_handshake_clear_fail_count() once init succeeds so
 * the bootloader's pre-jump fail_count bump is balanced out.  The slot
 * is determined from SCB->VTOR at runtime, which is the address the
 * bootloader programmed before jumping — same value for both slot-A
 * and slot-B builds without needing a compile-time switch.
 */

#include "stm32f4xx.h"

#include "bl_handshake.h"
#include "flash_slot.h"
#include "led2.h"
#include "systick.h"
#include "uart.h"

static flash_slot_id_t slot_from_vtor(void)
{
    return ((SCB->VTOR & ~0x1FFu) >= FLASH_SLOT_B_BASE) ? FLASH_SLOT_B
                                                        : FLASH_SLOT_A;
}

int main(void)
{
    /* SystemInit (called from Reset_Handler) already configured the clock. */
    led2_init();
    systick_init();
    uart_init();

    uart_puts("APP: blinky alive\r\n");

    /*
     * Tell the bootloader the boot was clean.  ERR_VERIFY is the
     * pristine-chip / unparseable-metadata case (no metadata yet)
     * and is harmless — the next bootloader pass will seed it.  Any
     * other failure means the metadata-sector erase or program
     * failed; log and keep running rather than panic the smoke test.
     */
    err_t hs = bl_handshake_clear_fail_count(slot_from_vtor());
    if (hs == ERR_OK) {
        uart_puts("APP: fc cleared\r\n");
    } else if (hs != ERR_VERIFY) {
        uart_puts("APP: clear_fail_count failed\r\n");
    }

    while (1) {
        led2_toggle();
        systick_delay_ms(250);
    }
}
