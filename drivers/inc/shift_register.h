#ifndef SHIFT_REGISTER_H_
#define SHIFT_REGISTER_H_

#include <stdint.h>

/**
 * @brief Initializes the SPI-based shift register driver (SN74HC595N)
 * 
 * This function configures:
 * - SPI1 peripheral in master mode
 * - GPIO pins for SPI1 (MOSI on PB5, SCK on PB3)
 * - GPIO pin for latch control (PA8)
 * 
 * SPI Configuration:
 * - 8-bit data frame
 * - MSB first
 * - Clock polarity: LOW when idle (CPOL=0)
 * - Clock phase: First edge (CPHA=0)
 * - Baud rate: ~1 MHz
 */
void shift_register_init(void);

/**
 * @brief Writes a byte to the shift register output
 * 
 * This function:
 * 1. Sets latch LOW to prepare for data transfer
 * 2. Sends the byte via SPI to the shift register
 * 3. Waits for transmission to complete
 * 4. Sets latch HIGH to transfer data to output pins
 * 
 * @param data The byte to write to the shift register outputs (Q0-Q7)
 */
void shift_register_write(uint8_t data);

#endif /* SHIFT_REGISTER_H_ */

