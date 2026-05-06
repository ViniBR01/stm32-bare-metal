/*
 * crc.c -- Hardware CRC-32 driver for STM32F411.
 *
 * Register summary (CRC at 0x4002 3000):
 *   DR  (0x00) -- Data register (read/write, 32-bit)
 *                  Write: feeds data into CRC computation
 *                  Read:  returns current CRC result
 *   IDR (0x04) -- Independent data register (8-bit, general-purpose storage)
 *   CR  (0x08) -- Control register
 *                  bit 0 (RESET) = reset CRC accumulator to 0xFFFFFFFF
 *
 * Clock: AHB1 (RCC->AHB1ENR bit 12 = CRCEN)
 * Polynomial: fixed 0x04C11DB7 (CRC-32/MPEG-2)
 * Initial value: 0xFFFFFFFF (after reset)
 */

#include "crc.h"
#include "stm32f4xx.h"

err_t crc_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_CRCEN;
    CRC->CR = CRC_CR_RESET;
    return ERR_OK;
}

void crc_reset(void)
{
    CRC->CR = CRC_CR_RESET;
}

uint32_t crc_accumulate(const uint32_t *data, uint32_t len)
{
    if (data == (void *)0 || len == 0) {
        return CRC->DR;
    }

    for (uint32_t i = 0; i < len; i++) {
        CRC->DR = data[i];
    }

    return CRC->DR;
}

uint32_t crc_get_result(void)
{
    return CRC->DR;
}
