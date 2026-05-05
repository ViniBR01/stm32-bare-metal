#include "flash.h"
#include <string.h>

#ifdef UNIT_TEST
/* Host test mode: redirect flash memory access to a fake buffer.
 * The buffer covers one 16 KB sector starting at FLASH_BASE_ADDR. */
#define FAKE_FLASH_SIZE  (16U * 1024U)
static uint8_t fake_flash_mem[FAKE_FLASH_SIZE];

void flash_test_reset(void)
{
    memset(fake_flash_mem, 0xFF, sizeof(fake_flash_mem));
}

static inline int addr_in_fake(uint32_t addr, size_t len)
{
    return (addr >= FLASH_BASE_ADDR) &&
           ((addr + len) <= (FLASH_BASE_ADDR + FAKE_FLASH_SIZE));
}

#define FLASH_WRITE_WORD(addr, val) do { \
    if (addr_in_fake((addr), 4)) { \
        uint32_t _v = (val); \
        memcpy(&fake_flash_mem[(addr) - FLASH_BASE_ADDR], &_v, 4); \
    } \
} while (0)

#define FLASH_WRITE_BYTE(addr, val) do { \
    if (addr_in_fake((addr), 1)) { \
        fake_flash_mem[(addr) - FLASH_BASE_ADDR] = (val); \
    } \
} while (0)

#define FLASH_READ_WORD(addr)  \
    (addr_in_fake((addr), 4) ? \
     (*(uint32_t *)&fake_flash_mem[(addr) - FLASH_BASE_ADDR]) : 0xDEADBEEFU)

#define FLASH_READ_BYTE(addr)  \
    (addr_in_fake((addr), 1) ? fake_flash_mem[(addr) - FLASH_BASE_ADDR] : 0xFFU)

#else
/* Real hardware: direct volatile access */
#define FLASH_WRITE_WORD(addr, val)  (*(volatile uint32_t *)(addr) = (val))
#define FLASH_WRITE_BYTE(addr, val)  (*(volatile uint8_t *)(addr) = (val))
#define FLASH_READ_WORD(addr)        (*(volatile uint32_t *)(addr))
#define FLASH_READ_BYTE(addr)        (*(volatile uint8_t *)(addr))
#endif

static const uint32_t sector_addresses[FLASH_SECTOR_COUNT] = {
    0x08000000U,  /* Sector 0: 16 KB */
    0x08004000U,  /* Sector 1: 16 KB */
    0x08008000U,  /* Sector 2: 16 KB */
    0x0800C000U,  /* Sector 3: 16 KB */
    0x08010000U,  /* Sector 4: 64 KB */
    0x08020000U,  /* Sector 5: 128 KB */
    0x08040000U,  /* Sector 6: 128 KB */
    0x08060000U,  /* Sector 7: 128 KB */
};

static const uint32_t sector_sizes[FLASH_SECTOR_COUNT] = {
    16U * 1024U,   /* Sector 0 */
    16U * 1024U,   /* Sector 1 */
    16U * 1024U,   /* Sector 2 */
    16U * 1024U,   /* Sector 3 */
    64U * 1024U,   /* Sector 4 */
    128U * 1024U,  /* Sector 5 */
    128U * 1024U,  /* Sector 6 */
    128U * 1024U,  /* Sector 7 */
};

static void flash_wait_bsy(void)
{
    while (FLASH->SR & FLASH_SR_BSY) { /* spin */ }
}

static err_t flash_check_errors(void)
{
    uint32_t sr = FLASH->SR;
    if (sr & FLASH_SR_ERROR_MASK) {
#ifdef UNIT_TEST
        FLASH->SR &= ~FLASH_SR_ERROR_MASK;
#else
        /* rc_w1: write 1 to error bits to clear them */
        FLASH->SR = sr & FLASH_SR_ERROR_MASK;
#endif
        return ERR_INVALID_ARG;
    }
    return ERR_OK;
}

err_t flash_unlock(void)
{
    if (!(FLASH->CR & FLASH_CR_LOCK)) {
        return ERR_OK;
    }

    FLASH->KEYR = FLASH_KEY1;
    FLASH->KEYR = FLASH_KEY2;

    if (FLASH->CR & FLASH_CR_LOCK) {
        return ERR_BUSY;
    }

    return ERR_OK;
}

void flash_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

err_t flash_erase_sector(uint8_t sector)
{
    if (sector > FLASH_SECTOR_MAX) {
        return ERR_INVALID_ARG;
    }

    flash_wait_bsy();

    FLASH->CR &= ~(FLASH_CR_PSIZE | FLASH_CR_SNB);
    FLASH->CR |= (FLASH_PSIZE_WORD << FLASH_CR_PSIZE_Pos)
               | ((uint32_t)sector << FLASH_CR_SNB_Pos)
               | FLASH_CR_SER;
    FLASH->CR |= FLASH_CR_STRT;

    flash_wait_bsy();

    FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_SNB);

    return flash_check_errors();
}

err_t flash_write_word(uint32_t address, uint32_t data)
{
    if ((address & 0x3U) != 0) {
        return ERR_INVALID_ARG;
    }
    if (address < FLASH_BASE_ADDR || address > (FLASH_END_ADDR - 3U)) {
        return ERR_INVALID_ARG;
    }

    flash_wait_bsy();

    FLASH->CR &= ~(FLASH_CR_PSIZE | FLASH_CR_SER | FLASH_CR_MER);
    FLASH->CR |= (FLASH_PSIZE_WORD << FLASH_CR_PSIZE_Pos) | FLASH_CR_PG;

    FLASH_WRITE_WORD(address, data);

    flash_wait_bsy();

    FLASH->CR &= ~FLASH_CR_PG;

    return flash_check_errors();
}

err_t flash_write_byte(uint32_t address, uint8_t data)
{
    if (address < FLASH_BASE_ADDR || address > FLASH_END_ADDR) {
        return ERR_INVALID_ARG;
    }

    flash_wait_bsy();

    FLASH->CR &= ~(FLASH_CR_PSIZE | FLASH_CR_SER | FLASH_CR_MER);
    FLASH->CR |= (FLASH_PSIZE_BYTE << FLASH_CR_PSIZE_Pos) | FLASH_CR_PG;

    FLASH_WRITE_BYTE(address, data);

    flash_wait_bsy();

    FLASH->CR &= ~FLASH_CR_PG;

    return flash_check_errors();
}

err_t flash_write_bytes(uint32_t address, const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        return ERR_INVALID_ARG;
    }
    if (address < FLASH_BASE_ADDR || (address + len - 1) > FLASH_END_ADDR) {
        return ERR_INVALID_ARG;
    }

    flash_wait_bsy();

    FLASH->CR &= ~(FLASH_CR_PSIZE | FLASH_CR_SER | FLASH_CR_MER);
    FLASH->CR |= (FLASH_PSIZE_BYTE << FLASH_CR_PSIZE_Pos) | FLASH_CR_PG;

    for (size_t i = 0; i < len; i++) {
        FLASH_WRITE_BYTE(address + i, data[i]);
        flash_wait_bsy();

        err_t err = flash_check_errors();
        if (err != ERR_OK) {
            FLASH->CR &= ~FLASH_CR_PG;
            return err;
        }
    }

    FLASH->CR &= ~FLASH_CR_PG;
    return ERR_OK;
}

err_t flash_read_word(uint32_t address, uint32_t *out)
{
    if (out == NULL) {
        return ERR_INVALID_ARG;
    }
    if ((address & 0x3U) != 0) {
        return ERR_INVALID_ARG;
    }
    if (address < FLASH_BASE_ADDR || address > (FLASH_END_ADDR - 3U)) {
        return ERR_INVALID_ARG;
    }

    *out = FLASH_READ_WORD(address);
    return ERR_OK;
}

err_t flash_read_bytes(uint32_t address, uint8_t *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return ERR_INVALID_ARG;
    }
    if (address < FLASH_BASE_ADDR || (address + len - 1) > FLASH_END_ADDR) {
        return ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < len; i++) {
        buf[i] = FLASH_READ_BYTE(address + i);
    }
    return ERR_OK;
}

uint32_t flash_get_sector_address(uint8_t sector)
{
    if (sector > FLASH_SECTOR_MAX) {
        return 0;
    }
    return sector_addresses[sector];
}

uint32_t flash_get_sector_size(uint8_t sector)
{
    if (sector > FLASH_SECTOR_MAX) {
        return 0;
    }
    return sector_sizes[sector];
}
