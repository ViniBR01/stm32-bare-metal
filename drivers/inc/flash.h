#ifndef FLASH_H
#define FLASH_H

#include <stdint.h>
#include <stddef.h>

#include "error.h"
#include "stm32f4xx.h"

/**
 * STM32F411 flash sector layout:
 *   Sector 0: 0x0800_0000 - 0x0800_3FFF (16 KB) — application code, PROTECTED
 *   Sector 1: 0x0800_4000 - 0x0800_7FFF (16 KB) — parameter storage
 *   Sector 2: 0x0800_8000 - 0x0800_BFFF (16 KB)
 *   Sector 3: 0x0800_C000 - 0x0800_FFFF (16 KB)
 *   Sector 4: 0x0801_0000 - 0x0801_FFFF (64 KB)
 *   Sector 5: 0x0802_0000 - 0x0803_FFFF (128 KB)
 *   Sector 6: 0x0804_0000 - 0x0805_FFFF (128 KB)
 *   Sector 7: 0x0806_0000 - 0x0807_FFFF (128 KB)
 */

#define FLASH_SECTOR_COUNT      8U
#define FLASH_SECTOR_MIN        0U
#define FLASH_SECTOR_MAX        7U

#define FLASH_BASE_ADDR         0x08000000U
#define FLASH_END_ADDR          0x0807FFFFU

#define FLASH_KEY1              0x45670123U
#define FLASH_KEY2              0xCDEF89ABU

#define FLASH_SR_ERROR_MASK     (FLASH_SR_PGSERR | FLASH_SR_PGPERR | \
                                 FLASH_SR_PGAERR | FLASH_SR_WRPERR)

typedef enum {
    FLASH_PSIZE_BYTE      = 0U,
    FLASH_PSIZE_HALFWORD  = 1U,
    FLASH_PSIZE_WORD      = 2U,
    FLASH_PSIZE_DOUBLEWORD = 3U,
} flash_psize_t;

/**
 * @brief Unlock the flash control register for programming/erasing.
 * @return ERR_OK on success, ERR_BUSY if already busy.
 */
err_t flash_unlock(void);

/**
 * @brief Lock the flash control register (re-enable write protection).
 */
void flash_lock(void);

/**
 * @brief Erase an entire flash sector.
 * @param sector  Sector number (0-7).
 * @return ERR_OK on success, ERR_INVALID_ARG for bad sector, or error from hardware.
 */
err_t flash_erase_sector(uint8_t sector);

/**
 * @brief Program a 32-bit word to flash.
 * @param address  Destination address (must be 4-byte aligned, within flash).
 * @param data     32-bit value to write.
 * @return ERR_OK on success, ERR_INVALID_ARG for misalignment/out-of-range.
 */
err_t flash_write_word(uint32_t address, uint32_t data);

/**
 * @brief Program a byte to flash.
 * @param address  Destination address (must be within flash).
 * @param data     Byte value to write.
 * @return ERR_OK on success, ERR_INVALID_ARG for out-of-range address.
 */
err_t flash_write_byte(uint32_t address, uint8_t data);

/**
 * @brief Program a buffer of bytes to flash.
 * @param address  Starting destination address (within flash).
 * @param data     Source buffer.
 * @param len      Number of bytes to write.
 * @return ERR_OK on success, ERR_INVALID_ARG for invalid range.
 */
err_t flash_write_bytes(uint32_t address, const uint8_t *data, size_t len);

/**
 * @brief Read a 32-bit word from flash.
 * @param address  Source address (must be 4-byte aligned, within flash).
 * @param out      Pointer to receive the read value.
 * @return ERR_OK on success, ERR_INVALID_ARG for misalignment/out-of-range/null.
 */
err_t flash_read_word(uint32_t address, uint32_t *out);

/**
 * @brief Read a buffer of bytes from flash.
 * @param address  Source address (within flash).
 * @param buf      Destination buffer.
 * @param len      Number of bytes to read.
 * @return ERR_OK on success, ERR_INVALID_ARG for invalid range or null buffer.
 */
err_t flash_read_bytes(uint32_t address, uint8_t *buf, size_t len);

/**
 * @brief Get the start address of a flash sector.
 * @param sector  Sector number (0-7).
 * @return Start address, or 0 if sector is invalid.
 */
uint32_t flash_get_sector_address(uint8_t sector);

/**
 * @brief Get the size (in bytes) of a flash sector.
 * @param sector  Sector number (0-7).
 * @return Size in bytes, or 0 if sector is invalid.
 */
uint32_t flash_get_sector_size(uint8_t sector);

/**
 * @brief Find the flash sector that contains a given absolute address.
 *
 * Inverse of `flash_get_sector_address()`.  Useful when code that has only
 * a pointer (e.g. the running image's vector base, taken from a linker
 * symbol) needs to assert it is not erasing its own sector.
 *
 * @param address  Absolute flash address.
 * @param sector_out  On success, receives the sector index (0..7).
 * @return ERR_OK if `address` is inside a sector, ERR_INVALID_ARG if it
 *         lies outside the on-chip flash region.
 */
err_t flash_sector_for_address(uint32_t address, uint8_t *sector_out);

#ifdef UNIT_TEST
void flash_test_reset(void);
#endif

#endif /* FLASH_H */
