#include "spi.h"
#include "gpio_handler.h"

/* Only include hardware headers when compiling for target */
#ifndef SPI_HOST_TEST
#include "dma.h"
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

    /* Configure pins: AF mode, high-speed slew rate for fast SPI clocks */
    gpio_configure_full(cfg->sck_port,  cfg->sck_pin,  GPIO_MODE_AF,
                        GPIO_OUTPUT_PUSH_PULL, GPIO_SPEED_HIGH, GPIO_PULL_NONE);
    gpio_configure_full(cfg->miso_port, cfg->miso_pin, GPIO_MODE_AF,
                        GPIO_OUTPUT_PUSH_PULL, GPIO_SPEED_HIGH, GPIO_PULL_NONE);
    gpio_configure_full(cfg->mosi_port, cfg->mosi_pin, GPIO_MODE_AF,
                        GPIO_OUTPUT_PUSH_PULL, GPIO_SPEED_HIGH, GPIO_PULL_NONE);

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

/* Forward declaration -- defined in the DMA section below */
static void spi_dma_release_for_deinit(spi_instance_t inst);

void spi_deinit(spi_handle_t *handle) {
    if (!handle || !handle->regs) return;

    spi_instance_t inst = handle->config.instance;
    SPI_TypeDef *spi = (SPI_TypeDef *)handle->regs;
    spi->CR1 &= ~SPI_CR1_SPE;

    /* Release DMA streams if they were allocated */
    if (inst < SPI_INSTANCE_COUNT) {
        spi_dma_release_for_deinit(inst);
    }

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

/*===========================================================================
 * DMA-based SPI transfer (via generic DMA driver)
 *
 * Stream/channel assignments from STM32F411 RM Table 28:
 *
 *  SPI1 (APB2) -- DMA2: TX Stream3/Ch3, RX Stream0/Ch3
 *  SPI2 (APB1) -- DMA1: TX Stream4/Ch0, RX Stream3/Ch0
 *  SPI3 (APB1) -- DMA1: TX Stream5/Ch0, RX Stream0/Ch0
 *  SPI4 (APB2) -- DMA2: TX Stream1/Ch4, RX Stream0/Ch4
 *  SPI5 (APB2) -- DMA2: TX Stream6/Ch7, RX Stream3/Ch2
 *===========================================================================*/

/**
 * @brief DMA stream/channel mapping for each SPI instance
 */
typedef struct {
    dma_stream_id_t tx_stream;
    dma_stream_id_t rx_stream;
    uint8_t         tx_channel;
    uint8_t         rx_channel;
} spi_dma_map_t;

static const spi_dma_map_t spi_dma_map[SPI_INSTANCE_COUNT] = {
    [SPI_INSTANCE_1] = { DMA_STREAM_2_3, DMA_STREAM_2_0, 3, 3 },
    [SPI_INSTANCE_2] = { DMA_STREAM_1_4, DMA_STREAM_1_3, 0, 0 },
    [SPI_INSTANCE_3] = { DMA_STREAM_1_5, DMA_STREAM_1_0, 0, 0 },
    [SPI_INSTANCE_4] = { DMA_STREAM_2_1, DMA_STREAM_2_0, 4, 4 },
    [SPI_INSTANCE_5] = { DMA_STREAM_2_6, DMA_STREAM_2_3, 7, 2 },
};

/**
 * @brief Active handle per SPI instance, used by DMA callbacks
 */
static spi_handle_t *spi_dma_active[SPI_INSTANCE_COUNT];

/**
 * @brief Tracks whether DMA streams have been allocated for each SPI instance.
 *
 * Streams are initialized on the first DMA transfer and kept allocated across
 * subsequent transfers to avoid the overhead of repeated NVIC and allocation
 * setup.  They are released only in spi_deinit().
 */
static uint8_t spi_dma_initialized[SPI_INSTANCE_COUNT];

/**
 * @brief Release DMA streams for a given SPI instance (called from spi_deinit)
 */
static void spi_dma_release_for_deinit(spi_instance_t inst) {
    if (!spi_dma_initialized[inst]) return;

    const spi_dma_map_t *map = &spi_dma_map[inst];
    dma_stream_release(map->rx_stream);
    dma_stream_release(map->tx_stream);
    spi_dma_initialized[inst] = 0;
}

/* Dummy bytes for NULL tx/rx DMA transfers */
static const uint8_t spi_dma_tx_dummy = 0xFF;
static uint8_t spi_dma_rx_dummy;

/**
 * @brief RX DMA transfer-complete callback -- performs SPI cleanup
 *
 * Called from DMA ISR context when the RX stream finishes.
 * Stops both streams (keeps them allocated) and signals completion.
 */
static void spi_dma_rx_complete_cb(dma_stream_id_t stream, void *ctx) {
    (void)stream;
    spi_handle_t *h = (spi_handle_t *)ctx;
    if (!h) return;

    spi_instance_t inst = h->config.instance;
    SPI_TypeDef *spi = (SPI_TypeDef *)h->regs;

    /* TX stream has already finished (its EN bit is 0) because in SPI
     * full-duplex the TX and RX counts are identical and TX clocks out
     * first.  Its flags will be cleared on the next start_config call,
     * so there is no need to call dma_stream_stop(tx) here. */

    /* Disable SPI DMA requests */
    spi->CR2 &= ~(SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN);

    /* Wait for SPI to finish, then disable */
    while (spi->SR & SPI_SR_BSY);
    spi->CR1 &= ~SPI_CR1_SPE;

    /* Signal completion -- streams stay allocated for the next transfer */
    spi_dma_active[inst] = (void *)0;
    h->dma_busy = 0;
}

/**
 * @brief One-time DMA stream allocation for a given SPI instance
 *
 * Called on the first spi_transfer_dma() invocation.  Subsequent transfers
 * skip this and go straight to dma_stream_set_mem_inc() + dma_stream_start().
 */
static int spi_dma_init_streams(spi_handle_t *handle) {
    spi_instance_t inst = handle->config.instance;
    SPI_TypeDef *spi = (SPI_TypeDef *)handle->regs;
    const spi_dma_map_t *map = &spi_dma_map[inst];

    /* RX stream (peripheral-to-memory) */
    dma_stream_config_t rx_cfg = {
        .stream        = map->rx_stream,
        .channel       = map->rx_channel,
        .direction     = DMA_DIR_PERIPH_TO_MEM,
        .periph_addr   = (uint32_t)&spi->DR,
        .mem_inc       = 1,
        .periph_inc    = 0,
        .circular      = 0,
        .priority      = DMA_PRIO_HIGH,
        .tc_callback   = spi_dma_rx_complete_cb,
        .error_callback = (void *)0,
        .cb_ctx        = handle,
        .nvic_priority = 1,
    };
    if (dma_stream_init(&rx_cfg) != 0) return -1;

    /* TX stream (memory-to-peripheral) */
    dma_stream_config_t tx_cfg = {
        .stream        = map->tx_stream,
        .channel       = map->tx_channel,
        .direction     = DMA_DIR_MEM_TO_PERIPH,
        .periph_addr   = (uint32_t)&spi->DR,
        .mem_inc       = 1,
        .periph_inc    = 0,
        .circular      = 0,
        .priority      = DMA_PRIO_HIGH,
        .tc_callback   = (void *)0,   /* Only need RX TC */
        .error_callback = (void *)0,
        .cb_ctx        = (void *)0,
        .nvic_priority = 1,
    };
    if (dma_stream_init(&tx_cfg) != 0) {
        dma_stream_release(map->rx_stream);
        return -1;
    }

    spi_dma_initialized[inst] = 1;
    return 0;
}

int spi_transfer_dma(spi_handle_t *handle, const uint8_t *tx, uint8_t *rx, uint16_t len) {
    if (!handle || !handle->regs || len == 0) return -1;
    if (handle->config.instance >= SPI_INSTANCE_COUNT) return -1;

    spi_instance_t inst = handle->config.instance;
    SPI_TypeDef *spi = (SPI_TypeDef *)handle->regs;
    const spi_dma_map_t *map = &spi_dma_map[inst];

    /* One-time stream allocation (first transfer only) */
    if (!spi_dma_initialized[inst]) {
        if (spi_dma_init_streams(handle) != 0) return -1;
    }

    /* Store handle so the callback can find it */
    handle->dma_busy = 1;
    spi_dma_active[inst] = handle;

    /* Enable SPI DMA requests */
    spi->CR2 |= SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN;

    /* Enable SPI peripheral */
    spi->CR1 |= SPI_CR1_SPE;

    /* Reconfigure MINC and start DMA in a single call per stream.
     * RX first so it is ready before TX begins clocking. */
    uint32_t rx_addr = rx ? (uint32_t)rx : (uint32_t)&spi_dma_rx_dummy;
    uint32_t tx_addr = tx ? (uint32_t)tx : (uint32_t)&spi_dma_tx_dummy;

    dma_stream_start_config(map->rx_stream, rx_addr, len, rx != (void *)0);
    dma_stream_start_config(map->tx_stream, tx_addr, len, tx != (void *)0);

    return 0;
}

int spi_transfer_dma_blocking(spi_handle_t *handle, const uint8_t *tx,
                              uint8_t *rx, uint16_t len) {
    int rc = spi_transfer_dma(handle, tx, rx, len);
    if (rc != 0) return rc;

    while (handle->dma_busy);

    return 0;
}

#endif /* SPI_HOST_TEST */
