/*
 * app_blinky_signed — Plan 001 Phase 1.5 smoke-test app.
 *
 * Trivial slot-A app linked at SLOT_BASE + sizeof(img_header_t).  The
 * bootloader jumps here after parsing the header.  Emits one boot-log line
 * over USART2 so the HIL boot smoke test can assert the boot chain works
 * end-to-end, then toggles LED2 forever.
 */

#include "led2.h"
#include "systick.h"
#include "uart.h"

static void uart_print(const char *s)
{
    while (*s) {
        uart_write(*s++);
    }
}

int main(void)
{
    /* SystemInit (called from Reset_Handler) already configured the clock. */
    led2_init();
    systick_init();
    uart_init();

    uart_print("APP: blinky alive\r\n");

    while (1) {
        led2_toggle();
        systick_delay_ms(250);
    }
}
