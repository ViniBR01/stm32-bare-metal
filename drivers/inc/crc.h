/*
 * crc.h -- Hardware CRC-32 driver for STM32F411.
 *
 * The STM32F411 CRC peripheral computes CRC-32 using a fixed polynomial
 * (0x04C11DB7, MPEG-2 variant) with initial value 0xFFFFFFFF. It accepts
 * 32-bit word input only. The result is NOT bit-reversed or XOR'd -- this
 * is NOT the same as Ethernet/ZIP CRC-32.
 *
 * Usage:
 *   crc_init();
 *   crc_reset();
 *   uint32_t crc = crc_accumulate(data, word_count);
 *   // crc now contains the CRC-32 of the input data
 */

#ifndef CRC_H
#define CRC_H

#include <stdint.h>
#include "error.h"

/**
 * @brief Initialise the CRC peripheral.
 *
 * Enables the AHB1 clock for the CRC unit and resets the accumulator
 * to the initial value (0xFFFFFFFF).
 *
 * @return ERR_OK on success
 */
err_t crc_init(void);

/**
 * @brief Reset the CRC accumulator to 0xFFFFFFFF.
 *
 * Call before starting a new CRC computation if the peripheral has
 * already been used.
 */
void crc_reset(void);

/**
 * @brief Feed 32-bit words into the CRC accumulator and return the result.
 *
 * Each word is written to the CRC data register in sequence. The hardware
 * computes the running CRC-32 automatically.
 *
 * @param data  Pointer to array of 32-bit words (must not be NULL if len > 0)
 * @param len   Number of 32-bit words to process
 * @return Current CRC-32 value after processing all words
 */
uint32_t crc_accumulate(const uint32_t *data, uint32_t len);

/**
 * @brief Read the current CRC result without feeding new data.
 *
 * @return Current CRC-32 accumulator value
 */
uint32_t crc_get_result(void);

#endif /* CRC_H */
