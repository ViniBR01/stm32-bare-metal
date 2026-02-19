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

/*===========================================================================
 * DMA-based SPI transfer
 *
 * Stream/channel assignments from STM32F411 RM Table 28.
 * UART2 TX owns DMA1_Stream6/Ch4 -- none of the entries below collide.
 *
 *  SPI1 (APB2) -- DMA2: TX Stream3/Ch3, RX Stream0/Ch3
 *  SPI2 (APB1) -- DMA1: TX Stream4/Ch0, RX Stream3/Ch0
 *  SPI3 (APB1) -- DMA1: TX Stream5/Ch0, RX Stream0/Ch0
 *  SPI4 (APB2) -- DMA2: TX Stream1/Ch4, RX Stream0/Ch4
 *  SPI5 (APB2) -- DMA2: TX Stream6/Ch7, RX Stream3/Ch2
 *===========================================================================*/

/**
 * @brief DMA resource descriptor for one SPI instance
 */
typedef struct {
    DMA_TypeDef          *dma;           /* DMA1 or DMA2 */
    DMA_Stream_TypeDef   *tx_stream;
    DMA_Stream_TypeDef   *rx_stream;
    uint32_t              tx_chsel;      /* CHSEL bits for TX stream CR */
    uint32_t              rx_chsel;      /* CHSEL bits for RX stream CR */
    IRQn_Type             rx_irqn;       /* NVIC IRQ for RX stream (completion) */
    /* Pointers to the correct status / clear-flag registers for each stream.
       Streams 0-3 use LISR/LIFCR; streams 4-7 use HISR/HIFCR. */
    volatile uint32_t    *rx_isr;        /* DMA->LISR or DMA->HISR */
    volatile uint32_t    *rx_ifcr;       /* DMA->LIFCR or DMA->HIFCR */
    volatile uint32_t    *tx_isr;
    volatile uint32_t    *tx_ifcr;
    uint32_t              rx_tcif;       /* TCIF mask for the RX stream */
    uint32_t              rx_tcif_clr;   /* CTCIF mask to clear RX TCIF */
    uint32_t              rx_all_clr;    /* Mask to clear all RX stream flags */
    uint32_t              tx_all_clr;    /* Mask to clear all TX stream flags */
    uint32_t              rcc_dma_bit;   /* RCC AHB1ENR enable bit */
} spi_dma_hw_info_t;

/* Helper: all-flags-clear mask for a stream within its IFCR register.
   Bit layout repeats every 6 bits for streams 0/4, 1/5, 2/6, 3/7.
   Pattern for stream N (mod 4): FEIF at base, DMEIF at base+2, TEIF +3,
   HTIF +4, TCIF +5. Bases: 0, 6, 16, 22. */
#define DMA_IFCR_ALL(base)  ( (0x1U << ((base)+0)) | (0x1U << ((base)+2)) | \
                              (0x1U << ((base)+3)) | (0x1U << ((base)+4)) | \
                              (0x1U << ((base)+5)) )
/* Stream-within-register bases (streams 0/4 -> 0, 1/5 -> 6, 2/6 -> 16, 3/7 -> 22) */
#define DMA_IFCR_BASE_0  0
#define DMA_IFCR_BASE_1  6
#define DMA_IFCR_BASE_2  16
#define DMA_IFCR_BASE_3  22

static const spi_dma_hw_info_t spi_dma_table[SPI_INSTANCE_COUNT] = {
    /* SPI1: DMA2, TX=Stream3/Ch3, RX=Stream0/Ch3 */
    [SPI_INSTANCE_1] = {
        .dma         = DMA2,
        .tx_stream   = DMA2_Stream3,
        .rx_stream   = DMA2_Stream0,
        .tx_chsel    = (3U << DMA_SxCR_CHSEL_Pos),
        .rx_chsel    = (3U << DMA_SxCR_CHSEL_Pos),
        .rx_irqn     = DMA2_Stream0_IRQn,
        .rx_isr      = &DMA2->LISR,
        .rx_ifcr     = &DMA2->LIFCR,
        .tx_isr      = &DMA2->LISR,
        .tx_ifcr     = &DMA2->LIFCR,
        .rx_tcif     = DMA_LISR_TCIF0,
        .rx_tcif_clr = DMA_LIFCR_CTCIF0,
        .rx_all_clr  = DMA_IFCR_ALL(DMA_IFCR_BASE_0),
        .tx_all_clr  = DMA_IFCR_ALL(DMA_IFCR_BASE_3),
        .rcc_dma_bit = RCC_AHB1ENR_DMA2EN,
    },
    /* SPI2: DMA1, TX=Stream4/Ch0, RX=Stream3/Ch0 */
    [SPI_INSTANCE_2] = {
        .dma         = DMA1,
        .tx_stream   = DMA1_Stream4,
        .rx_stream   = DMA1_Stream3,
        .tx_chsel    = (0U << DMA_SxCR_CHSEL_Pos),
        .rx_chsel    = (0U << DMA_SxCR_CHSEL_Pos),
        .rx_irqn     = DMA1_Stream3_IRQn,
        .rx_isr      = &DMA1->LISR,
        .rx_ifcr     = &DMA1->LIFCR,
        .tx_isr      = &DMA1->HISR,
        .tx_ifcr     = &DMA1->HIFCR,
        .rx_tcif     = DMA_LISR_TCIF3,
        .rx_tcif_clr = DMA_LIFCR_CTCIF3,
        .rx_all_clr  = DMA_IFCR_ALL(DMA_IFCR_BASE_3),
        .tx_all_clr  = DMA_IFCR_ALL(DMA_IFCR_BASE_0),
        .rcc_dma_bit = RCC_AHB1ENR_DMA1EN,
    },
    /* SPI3: DMA1, TX=Stream5/Ch0, RX=Stream0/Ch0 */
    [SPI_INSTANCE_3] = {
        .dma         = DMA1,
        .tx_stream   = DMA1_Stream5,
        .rx_stream   = DMA1_Stream0,
        .tx_chsel    = (0U << DMA_SxCR_CHSEL_Pos),
        .rx_chsel    = (0U << DMA_SxCR_CHSEL_Pos),
        .rx_irqn     = DMA1_Stream0_IRQn,
        .rx_isr      = &DMA1->LISR,
        .rx_ifcr     = &DMA1->LIFCR,
        .tx_isr      = &DMA1->HISR,
        .tx_ifcr     = &DMA1->HIFCR,
        .rx_tcif     = DMA_LISR_TCIF0,
        .rx_tcif_clr = DMA_LIFCR_CTCIF0,
        .rx_all_clr  = DMA_IFCR_ALL(DMA_IFCR_BASE_0),
        .tx_all_clr  = DMA_IFCR_ALL(DMA_IFCR_BASE_1),
        .rcc_dma_bit = RCC_AHB1ENR_DMA1EN,
    },
    /* SPI4: DMA2, TX=Stream1/Ch4, RX=Stream0/Ch4 */
    [SPI_INSTANCE_4] = {
        .dma         = DMA2,
        .tx_stream   = DMA2_Stream1,
        .rx_stream   = DMA2_Stream0,
        .tx_chsel    = (4U << DMA_SxCR_CHSEL_Pos),
        .rx_chsel    = (4U << DMA_SxCR_CHSEL_Pos),
        .rx_irqn     = DMA2_Stream0_IRQn,
        .rx_isr      = &DMA2->LISR,
        .rx_ifcr     = &DMA2->LIFCR,
        .tx_isr      = &DMA2->LISR,
        .tx_ifcr     = &DMA2->LIFCR,
        .rx_tcif     = DMA_LISR_TCIF0,
        .rx_tcif_clr = DMA_LIFCR_CTCIF0,
        .rx_all_clr  = DMA_IFCR_ALL(DMA_IFCR_BASE_0),
        .tx_all_clr  = DMA_IFCR_ALL(DMA_IFCR_BASE_1),
        .rcc_dma_bit = RCC_AHB1ENR_DMA2EN,
    },
    /* SPI5: DMA2, TX=Stream6/Ch7, RX=Stream3/Ch2 */
    [SPI_INSTANCE_5] = {
        .dma         = DMA2,
        .tx_stream   = DMA2_Stream6,
        .rx_stream   = DMA2_Stream3,
        .tx_chsel    = (7U << DMA_SxCR_CHSEL_Pos),
        .rx_chsel    = (2U << DMA_SxCR_CHSEL_Pos),
        .rx_irqn     = DMA2_Stream3_IRQn,
        .rx_isr      = &DMA2->LISR,
        .rx_ifcr     = &DMA2->LIFCR,
        .tx_isr      = &DMA2->HISR,
        .tx_ifcr     = &DMA2->HIFCR,
        .rx_tcif     = DMA_LISR_TCIF3,
        .rx_tcif_clr = DMA_LIFCR_CTCIF3,
        .rx_all_clr  = DMA_IFCR_ALL(DMA_IFCR_BASE_3),
        .tx_all_clr  = DMA_IFCR_ALL(DMA_IFCR_BASE_2),
        .rcc_dma_bit = RCC_AHB1ENR_DMA2EN,
    },
};

/**
 * @brief Active handle per SPI instance, used by DMA ISRs to find the handle
 */
static spi_handle_t *spi_dma_active[SPI_INSTANCE_COUNT];

/* Dummy bytes for NULL tx/rx DMA transfers */
static const uint8_t spi_dma_tx_dummy = 0xFF;
static uint8_t spi_dma_rx_dummy;

int spi_transfer_dma(spi_handle_t *handle, const uint8_t *tx, uint8_t *rx, uint16_t len) {
    if (!handle || !handle->regs || len == 0) return -1;
    if (handle->config.instance >= SPI_INSTANCE_COUNT) return -1;

    SPI_TypeDef *spi = (SPI_TypeDef *)handle->regs;
    const spi_dma_hw_info_t *dma_hw = &spi_dma_table[handle->config.instance];

    /* Enable DMA clock */
    RCC->AHB1ENR |= dma_hw->rcc_dma_bit;

    /* Disable both streams before configuration */
    dma_hw->tx_stream->CR &= ~DMA_SxCR_EN;
    dma_hw->rx_stream->CR &= ~DMA_SxCR_EN;
    while (dma_hw->tx_stream->CR & DMA_SxCR_EN);
    while (dma_hw->rx_stream->CR & DMA_SxCR_EN);

    /* Clear all pending interrupt flags for both streams */
    *dma_hw->rx_ifcr = dma_hw->rx_all_clr;
    *dma_hw->tx_ifcr = dma_hw->tx_all_clr;

    /* --- Configure RX stream (peripheral-to-memory) --- */
    dma_hw->rx_stream->CR = 0;
    dma_hw->rx_stream->PAR  = (uint32_t)&spi->DR;
    if (rx) {
        dma_hw->rx_stream->M0AR = (uint32_t)rx;
        dma_hw->rx_stream->CR   = dma_hw->rx_chsel
                                | DMA_SxCR_MINC    /* increment memory */
                                | DMA_SxCR_TCIE;   /* transfer-complete IRQ */
    } else {
        dma_hw->rx_stream->M0AR = (uint32_t)&spi_dma_rx_dummy;
        dma_hw->rx_stream->CR   = dma_hw->rx_chsel
                                | DMA_SxCR_TCIE;   /* no MINC -- discard to dummy */
    }
    dma_hw->rx_stream->NDTR = len;

    /* --- Configure TX stream (memory-to-peripheral) --- */
    dma_hw->tx_stream->CR = 0;
    dma_hw->tx_stream->PAR  = (uint32_t)&spi->DR;
    if (tx) {
        dma_hw->tx_stream->M0AR = (uint32_t)tx;
        dma_hw->tx_stream->CR   = dma_hw->tx_chsel
                                | DMA_SxCR_MINC              /* increment memory */
                                | (1U << DMA_SxCR_DIR_Pos);  /* mem-to-peripheral */
    } else {
        dma_hw->tx_stream->M0AR = (uint32_t)&spi_dma_tx_dummy;
        dma_hw->tx_stream->CR   = dma_hw->tx_chsel
                                | (1U << DMA_SxCR_DIR_Pos);  /* no MINC */
    }
    dma_hw->tx_stream->NDTR = len;

    /* Store handle so the ISR can find it */
    handle->dma_busy = 1;
    spi_dma_active[handle->config.instance] = handle;

    /* Enable NVIC for the RX-complete interrupt */
    NVIC_EnableIRQ(dma_hw->rx_irqn);

    /* Enable SPI DMA requests */
    spi->CR2 |= SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN;

    /* Enable SPI peripheral */
    spi->CR1 |= SPI_CR1_SPE;

    /* Start DMA: enable RX first so it is ready before TX begins clocking */
    dma_hw->rx_stream->CR |= DMA_SxCR_EN;
    dma_hw->tx_stream->CR |= DMA_SxCR_EN;

    return 0;
}

int spi_transfer_dma_blocking(spi_handle_t *handle, const uint8_t *tx,
                              uint8_t *rx, uint16_t len) {
    int rc = spi_transfer_dma(handle, tx, rx, len);
    if (rc != 0) return rc;

    while (handle->dma_busy);

    return 0;
}

/*---------------------------------------------------------------------------
 * DMA RX-complete ISR helpers
 *
 * Each RX stream used above needs an ISR that:
 *   1. Checks/clears the TCIF for its stream
 *   2. Disables TX and RX DMA streams
 *   3. Clears SPI RXDMAEN/TXDMAEN
 *   4. Waits for BSY=0, clears SPE
 *   5. Sets dma_busy=0 and clears the active-handle slot
 *
 * A shared helper does the real work; the per-stream ISR just locates the
 * correct SPI instance.
 *---------------------------------------------------------------------------*/

static void spi_dma_rx_complete(spi_instance_t inst) {
    spi_handle_t *h = spi_dma_active[inst];
    if (!h) return;

    const spi_dma_hw_info_t *dma_hw = &spi_dma_table[inst];
    SPI_TypeDef *spi = (SPI_TypeDef *)h->regs;

    /* Clear RX TCIF */
    *dma_hw->rx_ifcr = dma_hw->rx_tcif_clr;

    /* Disable both DMA streams */
    dma_hw->tx_stream->CR &= ~DMA_SxCR_EN;
    dma_hw->rx_stream->CR &= ~DMA_SxCR_EN;

    /* Disable SPI DMA requests */
    spi->CR2 &= ~(SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN);

    /* Wait for SPI to finish, then disable */
    while (spi->SR & SPI_SR_BSY);
    spi->CR1 &= ~SPI_CR1_SPE;

    /* Signal completion */
    spi_dma_active[inst] = (void *)0;
    h->dma_busy = 0;
}

/*
 * DMA2_Stream0 is used as RX for SPI1 (Ch3) and SPI4 (Ch4).
 * Only one can be active at a time; check which handle is live.
 */
void DMA2_Stream0_IRQHandler(void) {
    if (spi_dma_active[SPI_INSTANCE_1]) {
        spi_dma_rx_complete(SPI_INSTANCE_1);
    } else if (spi_dma_active[SPI_INSTANCE_4]) {
        spi_dma_rx_complete(SPI_INSTANCE_4);
    }
}

/* DMA1_Stream3 -- RX for SPI2 */
void DMA1_Stream3_IRQHandler(void) {
    if (spi_dma_active[SPI_INSTANCE_2]) {
        spi_dma_rx_complete(SPI_INSTANCE_2);
    }
}

/* DMA1_Stream0 -- RX for SPI3 */
void DMA1_Stream0_IRQHandler(void) {
    if (spi_dma_active[SPI_INSTANCE_3]) {
        spi_dma_rx_complete(SPI_INSTANCE_3);
    }
}

/* DMA2_Stream3 -- RX for SPI5 */
void DMA2_Stream3_IRQHandler(void) {
    if (spi_dma_active[SPI_INSTANCE_5]) {
        spi_dma_rx_complete(SPI_INSTANCE_5);
    }
}

#endif /* SPI_HOST_TEST */
