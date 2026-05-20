/*
 * crc_basic.c -- Hardware CRC-32 basic example.
 *
 * Demonstrates the CRC driver by:
 *   1. Initialising the CRC peripheral.
 *   2. Computing CRC-32 of a known test buffer.
 *   3. Computing CRC-32 of the first 1 KB of flash (firmware itself).
 *   4. Blinking LED2 to indicate success.
 *
 * The STM32F411 CRC peripheral uses polynomial 0x04C11DB7 (MPEG-2 variant)
 * with initial value 0xFFFFFFFF. It processes 32-bit words only.
 */

#include "crc.h"
#include "led2.h"
#include "systick.h"

#define BLINK_COUNT     5U
#define BLINK_MS        200U

static const uint32_t test_data[] = {
    0x00000001, 0x00000002, 0x00000003, 0x00000004,
    0x05060708, 0x090A0B0C, 0x0D0E0F10, 0xDEADBEEF
};

int main(void)
{
    led2_init();
    systick_init();

    crc_init();

    /* Compute CRC of test buffer */
    crc_reset();
    volatile uint32_t crc_test = crc_accumulate(test_data, 8);
    (void)crc_test;

    /* Compute CRC of first 256 words (1 KB) of flash */
    crc_reset();
    volatile uint32_t crc_flash = crc_accumulate((const uint32_t *)0x08000000U, 256);
    (void)crc_flash;

    /* Blink LED to indicate completion */
    for (uint32_t i = 0; i < BLINK_COUNT; i++) {
        led2_toggle();
        systick_delay_ms(BLINK_MS);
    }

    while (1) {
        /* Idle */
    }
}
