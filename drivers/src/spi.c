#include "spi.h"
#include "gpio_handler.h"

/* Only include hardware headers when compiling for target */
#ifndef SPI_HOST_TEST
#include "stm32f4xx.h"
#endif

/*===========================================================================
 * Pure logic functions (no hardware dependencies, testable on host)
 *===========================================================================*/

int spi_prescaler_to_br(uint16_t prescaler) {
    switch (prescaler) {
        case 2:   return 0;
        case 4:   return 1;
        case 8:   return 2;
        case 16:  return 3;
        case 32:  return 4;
        case 64:  return 5;
        case 128: return 6;
        case 256: return 7;
        default:  return -1;
    }
}

/*===========================================================================
 * Hardware-dependent code (excluded from host tests)
 *===========================================================================*/
#ifndef SPI_HOST_TEST

/**
 * @brief SPI peripheral descriptor used for instance-to-register lookup
 */
typedef struct {
    SPI_TypeDef    *regs;
    volatile uint32_t *rcc_enr;    /* Pointer to the RCC enable register */
    uint32_t        rcc_en_bit;    /* Bit mask within the enable register */
} spi_hw_info_t;

/**
 * @brief Lookup table mapping spi_instance_t to hardware resources
 */
static const spi_hw_info_t spi_hw_table[SPI_INSTANCE_COUNT] = {
    [SPI_INSTANCE_1] = { SPI1, &RCC->APB2ENR, RCC_APB2ENR_SPI1EN },
    [SPI_INSTANCE_2] = { SPI2, &RCC->APB1ENR, RCC_APB1ENR_SPI2EN },
    [SPI_INSTANCE_3] = { SPI3, &RCC->APB1ENR, RCC_APB1ENR_SPI3EN },
    [SPI_INSTANCE_4] = { SPI4, &RCC->APB2ENR, RCC_APB2ENR_SPI4EN },
    [SPI_INSTANCE_5] = { SPI5, &RCC->APB2ENR, RCC_APB2ENR_SPI5EN },
};

/**
 * @brief Configure the three SPI GPIO pins (SCK, MISO, MOSI) in AF mode
 */
static void spi_gpio_init(const spi_config_t *cfg) {
    /* Enable GPIO clocks for all three pins */
    gpio_clock_enable(cfg->sck_port);
    gpio_clock_enable(cfg->miso_port);
    gpio_clock_enable(cfg->mosi_port);

    /* Set pins to alternate function mode */
    gpio_configure_pin(cfg->sck_port,  cfg->sck_pin,  GPIO_MODE_AF);
    gpio_configure_pin(cfg->miso_port, cfg->miso_pin, GPIO_MODE_AF);
    gpio_configure_pin(cfg->mosi_port, cfg->mosi_pin, GPIO_MODE_AF);

    /* Assign per-pin alternate function numbers */
    gpio_set_af(cfg->sck_port,  cfg->sck_pin,  cfg->sck_af);
    gpio_set_af(cfg->miso_port, cfg->miso_pin, cfg->miso_af);
    gpio_set_af(cfg->mosi_port, cfg->mosi_pin, cfg->mosi_af);
}

int spi_init(spi_handle_t *handle, const spi_config_t *config) {
    if (!handle || !config) return -1;
    if (config->instance >= SPI_INSTANCE_COUNT) return -1;
    if (config->prescaler_br > 7) return -1;

    const spi_hw_info_t *hw = &spi_hw_table[config->instance];

    handle->regs   = hw->regs;
    handle->config = *config;

    /* Enable SPI peripheral clock */
    *hw->rcc_enr |= hw->rcc_en_bit;

    /* Configure GPIO pins */
    spi_gpio_init(config);

    /* Configure SPI as master: 8-bit, MSB first, SSM/SSI, selected BR/CPOL/CPHA */
    SPI_TypeDef *spi = hw->regs;
    spi->CR1 = 0;
    spi->CR1 = SPI_CR1_MSTR
             | SPI_CR1_SSM
             | SPI_CR1_SSI
             | ((uint32_t)config->prescaler_br << SPI_CR1_BR_Pos)
             | (config->cpol ? SPI_CR1_CPOL : 0)
             | (config->cpha ? SPI_CR1_CPHA : 0);

    return 0;
}

void spi_deinit(spi_handle_t *handle) {
    if (!handle || !handle->regs) return;

    SPI_TypeDef *spi = (SPI_TypeDef *)handle->regs;
    spi->CR1 &= ~SPI_CR1_SPE;

    /* Reset GPIO pins to input mode (default state after reset) */
    gpio_configure_pin(handle->config.sck_port,  handle->config.sck_pin,  GPIO_MODE_INPUT);
    gpio_configure_pin(handle->config.miso_port, handle->config.miso_pin, GPIO_MODE_INPUT);
    gpio_configure_pin(handle->config.mosi_port, handle->config.mosi_pin, GPIO_MODE_INPUT);

    const spi_hw_info_t *hw = &spi_hw_table[handle->config.instance];
    *hw->rcc_enr &= ~hw->rcc_en_bit;

    handle->regs = (void *)0;
}

void spi_enable(spi_handle_t *handle) {
    if (!handle || !handle->regs) return;
    SPI_TypeDef *spi = (SPI_TypeDef *)handle->regs;
    spi->CR1 |= SPI_CR1_SPE;
}

void spi_disable(spi_handle_t *handle) {
    if (!handle || !handle->regs) return;
    SPI_TypeDef *spi = (SPI_TypeDef *)handle->regs;
    spi->CR1 &= ~SPI_CR1_SPE;
}

int spi_transfer(spi_handle_t *handle, const uint8_t *tx, uint8_t *rx, uint16_t len) {
    if (!handle || !handle->regs || len == 0) return -1;

    SPI_TypeDef *spi = (SPI_TypeDef *)handle->regs;

    /* Enable SPI */
    spi->CR1 |= SPI_CR1_SPE;

    for (uint16_t i = 0; i < len; i++) {
        /* Wait for TX buffer empty, then write */
        while (!(spi->SR & SPI_SR_TXE));
        spi->DR = tx ? tx[i] : 0xFF;

        /* Wait for RX buffer not empty, then read */
        while (!(spi->SR & SPI_SR_RXNE));
        uint8_t byte = (uint8_t)(spi->DR & 0xFF);
        if (rx) rx[i] = byte;
    }

    /* Wait until not busy */
    while (spi->SR & SPI_SR_BSY);

    /* Disable SPI */
    spi->CR1 &= ~SPI_CR1_SPE;

    return 0;
}

#endif /* SPI_HOST_TEST */
