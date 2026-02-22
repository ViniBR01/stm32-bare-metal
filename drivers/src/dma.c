#include "dma.h"
#include "stm32f4xx.h"

/*===========================================================================
 * Internal types and data
 *===========================================================================*/

/**
 * @brief Hardware descriptor for one DMA stream
 *
 * All pointers and masks are pre-computed so the ISR path is fast.
 */
typedef struct {
    DMA_Stream_TypeDef *stream_regs;  /* e.g. DMA1_Stream6            */
    DMA_TypeDef        *dma_regs;     /* DMA1 or DMA2                 */
    IRQn_Type           irqn;         /* NVIC IRQ number              */
    volatile uint32_t  *isr_reg;      /* &DMA->LISR or &DMA->HISR    */
    volatile uint32_t  *ifcr_reg;     /* &DMA->LIFCR or &DMA->HIFCR  */
    uint32_t            tcif_mask;    /* TCIF bit in ISR register     */
    uint32_t            teif_mask;    /* TEIF bit in ISR register     */
    uint32_t            dmeif_mask;   /* DMEIF bit in ISR register    */
    uint32_t            feif_mask;    /* FEIF bit in ISR register     */
    uint32_t            all_clr_mask; /* Mask to clear all flags in IFCR */
    uint32_t            rcc_en_bit;   /* RCC AHB1ENR enable bit       */
} dma_hw_info_t;

/**
 * @brief Runtime state for one DMA stream
 */
typedef struct {
    uint8_t          allocated;       /* 1 = claimed by a driver      */
    uint32_t         cr_base;         /* CR value programmed at init (without EN) */
    dma_callback_t   tc_callback;
    dma_callback_t   error_callback;
    void            *cb_ctx;
} dma_stream_state_t;

/*---------------------------------------------------------------------------
 * Flag-bit pattern within LISR/HISR (and corresponding LIFCR/HIFCR):
 *
 *   Streams 0,4 use base offset  0  (bits  0.. 5)
 *   Streams 1,5 use base offset  6  (bits  6..11)
 *   Streams 2,6 use base offset 16  (bits 16..21)
 *   Streams 3,7 use base offset 22  (bits 22..27)
 *
 * Within each group the layout is:
 *   FEIF  at base+0
 *   DMEIF at base+2
 *   TEIF  at base+3
 *   HTIF  at base+4
 *   TCIF  at base+5
 *---------------------------------------------------------------------------*/
#define FLAG_BASE_0   0
#define FLAG_BASE_1   6
#define FLAG_BASE_2  16
#define FLAG_BASE_3  22

#define TCIF(base)   (1U << ((base) + 5))
#define HTIF(base)   (1U << ((base) + 4))
#define TEIF(base)   (1U << ((base) + 3))
#define DMEIF(base)  (1U << ((base) + 2))
#define FEIF(base)   (1U << ((base) + 0))
#define ALL_FLAGS(base) (TCIF(base) | HTIF(base) | TEIF(base) | DMEIF(base) | FEIF(base))

/*---------------------------------------------------------------------------
 * Hardware lookup table -- one entry per stream (16 total)
 *
 * Index matches dma_stream_id_t.
 *---------------------------------------------------------------------------*/
static const dma_hw_info_t hw_table[DMA_STREAM_COUNT] = {
    /* --- DMA1 streams 0-7 ------------------------------------------------*/
    [DMA_STREAM_1_0] = {
        DMA1_Stream0, DMA1, DMA1_Stream0_IRQn,
        &DMA1->LISR, &DMA1->LIFCR,
        TCIF(FLAG_BASE_0), TEIF(FLAG_BASE_0), DMEIF(FLAG_BASE_0), FEIF(FLAG_BASE_0),
        ALL_FLAGS(FLAG_BASE_0), RCC_AHB1ENR_DMA1EN,
    },
    [DMA_STREAM_1_1] = {
        DMA1_Stream1, DMA1, DMA1_Stream1_IRQn,
        &DMA1->LISR, &DMA1->LIFCR,
        TCIF(FLAG_BASE_1), TEIF(FLAG_BASE_1), DMEIF(FLAG_BASE_1), FEIF(FLAG_BASE_1),
        ALL_FLAGS(FLAG_BASE_1), RCC_AHB1ENR_DMA1EN,
    },
    [DMA_STREAM_1_2] = {
        DMA1_Stream2, DMA1, DMA1_Stream2_IRQn,
        &DMA1->LISR, &DMA1->LIFCR,
        TCIF(FLAG_BASE_2), TEIF(FLAG_BASE_2), DMEIF(FLAG_BASE_2), FEIF(FLAG_BASE_2),
        ALL_FLAGS(FLAG_BASE_2), RCC_AHB1ENR_DMA1EN,
    },
    [DMA_STREAM_1_3] = {
        DMA1_Stream3, DMA1, DMA1_Stream3_IRQn,
        &DMA1->LISR, &DMA1->LIFCR,
        TCIF(FLAG_BASE_3), TEIF(FLAG_BASE_3), DMEIF(FLAG_BASE_3), FEIF(FLAG_BASE_3),
        ALL_FLAGS(FLAG_BASE_3), RCC_AHB1ENR_DMA1EN,
    },
    [DMA_STREAM_1_4] = {
        DMA1_Stream4, DMA1, DMA1_Stream4_IRQn,
        &DMA1->HISR, &DMA1->HIFCR,
        TCIF(FLAG_BASE_0), TEIF(FLAG_BASE_0), DMEIF(FLAG_BASE_0), FEIF(FLAG_BASE_0),
        ALL_FLAGS(FLAG_BASE_0), RCC_AHB1ENR_DMA1EN,
    },
    [DMA_STREAM_1_5] = {
        DMA1_Stream5, DMA1, DMA1_Stream5_IRQn,
        &DMA1->HISR, &DMA1->HIFCR,
        TCIF(FLAG_BASE_1), TEIF(FLAG_BASE_1), DMEIF(FLAG_BASE_1), FEIF(FLAG_BASE_1),
        ALL_FLAGS(FLAG_BASE_1), RCC_AHB1ENR_DMA1EN,
    },
    [DMA_STREAM_1_6] = {
        DMA1_Stream6, DMA1, DMA1_Stream6_IRQn,
        &DMA1->HISR, &DMA1->HIFCR,
        TCIF(FLAG_BASE_2), TEIF(FLAG_BASE_2), DMEIF(FLAG_BASE_2), FEIF(FLAG_BASE_2),
        ALL_FLAGS(FLAG_BASE_2), RCC_AHB1ENR_DMA1EN,
    },
    [DMA_STREAM_1_7] = {
        DMA1_Stream7, DMA1, DMA1_Stream7_IRQn,
        &DMA1->HISR, &DMA1->HIFCR,
        TCIF(FLAG_BASE_3), TEIF(FLAG_BASE_3), DMEIF(FLAG_BASE_3), FEIF(FLAG_BASE_3),
        ALL_FLAGS(FLAG_BASE_3), RCC_AHB1ENR_DMA1EN,
    },
    /* --- DMA2 streams 0-7 ------------------------------------------------*/
    [DMA_STREAM_2_0] = {
        DMA2_Stream0, DMA2, DMA2_Stream0_IRQn,
        &DMA2->LISR, &DMA2->LIFCR,
        TCIF(FLAG_BASE_0), TEIF(FLAG_BASE_0), DMEIF(FLAG_BASE_0), FEIF(FLAG_BASE_0),
        ALL_FLAGS(FLAG_BASE_0), RCC_AHB1ENR_DMA2EN,
    },
    [DMA_STREAM_2_1] = {
        DMA2_Stream1, DMA2, DMA2_Stream1_IRQn,
        &DMA2->LISR, &DMA2->LIFCR,
        TCIF(FLAG_BASE_1), TEIF(FLAG_BASE_1), DMEIF(FLAG_BASE_1), FEIF(FLAG_BASE_1),
        ALL_FLAGS(FLAG_BASE_1), RCC_AHB1ENR_DMA2EN,
    },
    [DMA_STREAM_2_2] = {
        DMA2_Stream2, DMA2, DMA2_Stream2_IRQn,
        &DMA2->LISR, &DMA2->LIFCR,
        TCIF(FLAG_BASE_2), TEIF(FLAG_BASE_2), DMEIF(FLAG_BASE_2), FEIF(FLAG_BASE_2),
        ALL_FLAGS(FLAG_BASE_2), RCC_AHB1ENR_DMA2EN,
    },
    [DMA_STREAM_2_3] = {
        DMA2_Stream3, DMA2, DMA2_Stream3_IRQn,
        &DMA2->LISR, &DMA2->LIFCR,
        TCIF(FLAG_BASE_3), TEIF(FLAG_BASE_3), DMEIF(FLAG_BASE_3), FEIF(FLAG_BASE_3),
        ALL_FLAGS(FLAG_BASE_3), RCC_AHB1ENR_DMA2EN,
    },
    [DMA_STREAM_2_4] = {
        DMA2_Stream4, DMA2, DMA2_Stream4_IRQn,
        &DMA2->HISR, &DMA2->HIFCR,
        TCIF(FLAG_BASE_0), TEIF(FLAG_BASE_0), DMEIF(FLAG_BASE_0), FEIF(FLAG_BASE_0),
        ALL_FLAGS(FLAG_BASE_0), RCC_AHB1ENR_DMA2EN,
    },
    [DMA_STREAM_2_5] = {
        DMA2_Stream5, DMA2, DMA2_Stream5_IRQn,
        &DMA2->HISR, &DMA2->HIFCR,
        TCIF(FLAG_BASE_1), TEIF(FLAG_BASE_1), DMEIF(FLAG_BASE_1), FEIF(FLAG_BASE_1),
        ALL_FLAGS(FLAG_BASE_1), RCC_AHB1ENR_DMA2EN,
    },
    [DMA_STREAM_2_6] = {
        DMA2_Stream6, DMA2, DMA2_Stream6_IRQn,
        &DMA2->HISR, &DMA2->HIFCR,
        TCIF(FLAG_BASE_2), TEIF(FLAG_BASE_2), DMEIF(FLAG_BASE_2), FEIF(FLAG_BASE_2),
        ALL_FLAGS(FLAG_BASE_2), RCC_AHB1ENR_DMA2EN,
    },
    [DMA_STREAM_2_7] = {
        DMA2_Stream7, DMA2, DMA2_Stream7_IRQn,
        &DMA2->HISR, &DMA2->HIFCR,
        TCIF(FLAG_BASE_3), TEIF(FLAG_BASE_3), DMEIF(FLAG_BASE_3), FEIF(FLAG_BASE_3),
        ALL_FLAGS(FLAG_BASE_3), RCC_AHB1ENR_DMA2EN,
    },
};

/* Runtime state -- one entry per stream */
static dma_stream_state_t stream_state[DMA_STREAM_COUNT];

/*===========================================================================
 * Public API
 *===========================================================================*/

int dma_stream_init(const dma_stream_config_t *cfg) {
    if (!cfg || cfg->stream >= DMA_STREAM_COUNT || cfg->channel > 7) {
        return -1;
    }

    dma_stream_id_t id = cfg->stream;

    /* Conflict detection: reject if already allocated */
    if (stream_state[id].allocated) {
        return -1;
    }

    const dma_hw_info_t *hw = &hw_table[id];
    DMA_Stream_TypeDef *s = hw->stream_regs;

    /* Enable DMA controller clock */
    RCC->AHB1ENR |= hw->rcc_en_bit;

    /* Disable stream before configuration */
    s->CR &= ~DMA_SxCR_EN;
    while (s->CR & DMA_SxCR_EN);

    /* Clear all pending interrupt flags */
    *hw->ifcr_reg = hw->all_clr_mask;

    /* Build CR value: channel, direction, MINC, PINC, CIRC, priority,
       transfer-complete interrupt, error interrupts (TE + DME) */
    uint32_t cr = 0;
    cr |= ((uint32_t)cfg->channel << DMA_SxCR_CHSEL_Pos);
    cr |= ((uint32_t)cfg->direction << DMA_SxCR_DIR_Pos);
    cr |= ((uint32_t)cfg->priority << DMA_SxCR_PL_Pos);
    if (cfg->mem_inc)    cr |= DMA_SxCR_MINC;
    if (cfg->periph_inc) cr |= DMA_SxCR_PINC;
    if (cfg->circular)   cr |= DMA_SxCR_CIRC;
    if (cfg->tc_callback)    cr |= DMA_SxCR_TCIE;
    if (cfg->error_callback) cr |= DMA_SxCR_TEIE | DMA_SxCR_DMEIE;

    /* Write configuration (EN stays 0) */
    s->CR = cr;

    /* Set peripheral address */
    s->PAR = cfg->periph_addr;

    /* Store runtime state */
    stream_state[id].allocated      = 1;
    stream_state[id].cr_base        = cr;
    stream_state[id].tc_callback    = cfg->tc_callback;
    stream_state[id].error_callback = cfg->error_callback;
    stream_state[id].cb_ctx         = cfg->cb_ctx;

    /* Configure and enable NVIC */
    NVIC_SetPriority(hw->irqn, cfg->nvic_priority);
    NVIC_EnableIRQ(hw->irqn);

    return 0;
}

int dma_stream_start(dma_stream_id_t id, uint32_t mem_addr, uint16_t count) {
    if (id >= DMA_STREAM_COUNT || !stream_state[id].allocated) {
        return -1;
    }

    const dma_hw_info_t *hw = &hw_table[id];
    DMA_Stream_TypeDef *s = hw->stream_regs;

    /* Disable stream before reconfiguring */
    s->CR &= ~DMA_SxCR_EN;
    while (s->CR & DMA_SxCR_EN);

    /* Clear all interrupt flags */
    *hw->ifcr_reg = hw->all_clr_mask;

    /* Set memory address and transfer count */
    s->M0AR = mem_addr;
    s->NDTR = count;

    /* Enable stream */
    s->CR |= DMA_SxCR_EN;

    return 0;
}

void dma_stream_stop(dma_stream_id_t id) {
    if (id >= DMA_STREAM_COUNT) return;

    const dma_hw_info_t *hw = &hw_table[id];
    DMA_Stream_TypeDef *s = hw->stream_regs;

    s->CR &= ~DMA_SxCR_EN;
    while (s->CR & DMA_SxCR_EN);

    /* Clear all interrupt flags */
    *hw->ifcr_reg = hw->all_clr_mask;
}

void dma_stream_release(dma_stream_id_t id) {
    if (id >= DMA_STREAM_COUNT) return;

    dma_stream_stop(id);

    /* Disable NVIC for this stream */
    NVIC_DisableIRQ(hw_table[id].irqn);

    /* Clear runtime state */
    stream_state[id].allocated      = 0;
    stream_state[id].cr_base        = 0;
    stream_state[id].tc_callback    = (void *)0;
    stream_state[id].error_callback = (void *)0;
    stream_state[id].cb_ctx         = (void *)0;
}

void dma_stream_set_mem_inc(dma_stream_id_t id, uint8_t enable) {
    if (id >= DMA_STREAM_COUNT || !stream_state[id].allocated) return;

    DMA_Stream_TypeDef *s = hw_table[id].stream_regs;

    if (enable) {
        stream_state[id].cr_base |=  DMA_SxCR_MINC;
    } else {
        stream_state[id].cr_base &= ~DMA_SxCR_MINC;
    }

    /* Write updated CR to hardware (stream must be stopped, EN stays 0) */
    s->CR = stream_state[id].cr_base;
}

int dma_stream_start_config(dma_stream_id_t id, uint32_t mem_addr,
                            uint16_t count, uint8_t mem_inc) {
    if (id >= DMA_STREAM_COUNT || !stream_state[id].allocated) {
        return -1;
    }

    const dma_hw_info_t *hw = &hw_table[id];
    DMA_Stream_TypeDef *s = hw->stream_regs;

    /* Update MINC in cached CR */
    uint32_t cr = stream_state[id].cr_base;
    if (mem_inc) {
        cr |=  DMA_SxCR_MINC;
    } else {
        cr &= ~DMA_SxCR_MINC;
    }
    stream_state[id].cr_base = cr;

    /* Clear all interrupt flags (stream is already stopped) */
    *hw->ifcr_reg = hw->all_clr_mask;

    /* Write CR (with EN=0), memory address, and transfer count */
    s->CR   = cr;
    s->M0AR = mem_addr;
    s->NDTR = count;

    /* Enable stream -- single write, no read-modify-write */
    s->CR = cr | DMA_SxCR_EN;

    return 0;
}

int dma_stream_busy(dma_stream_id_t id) {
    if (id >= DMA_STREAM_COUNT) return 0;
    return (hw_table[id].stream_regs->CR & DMA_SxCR_EN) ? 1 : 0;
}

uint16_t dma_stream_get_ndtr(dma_stream_id_t id) {
    if (id >= DMA_STREAM_COUNT) return 0;
    return (uint16_t)hw_table[id].stream_regs->NDTR;
}

/*===========================================================================
 * ISR dispatch
 *
 * A single shared handler does the real work; per-stream ISR symbols just
 * forward with the correct stream id.
 *===========================================================================*/

static void dma_irq_handler(dma_stream_id_t id) {
    const dma_hw_info_t *hw = &hw_table[id];
    dma_stream_state_t  *st = &stream_state[id];
    uint32_t isr = *hw->isr_reg;

    /* --- Error flags (TE, DME, FE) --- */
    if (isr & (hw->teif_mask | hw->dmeif_mask | hw->feif_mask)) {
        /* Clear all error flags */
        *hw->ifcr_reg = hw->teif_mask | hw->dmeif_mask | hw->feif_mask;

        if (st->error_callback) {
            st->error_callback(id, st->cb_ctx);
        }
    }

    /* --- Transfer complete --- */
    if (isr & hw->tcif_mask) {
        /* Clear TC flag */
        *hw->ifcr_reg = hw->tcif_mask;

        if (st->tc_callback) {
            st->tc_callback(id, st->cb_ctx);
        }
    }
}

/*---------------------------------------------------------------------------
 * DMA1 stream ISR handlers
 *
 * __attribute__((used)) prevents LTO from discarding these strong
 * definitions before the linker resolves the weak aliases in the
 * vector table.
 *---------------------------------------------------------------------------*/
void __attribute__((used)) DMA1_Stream0_IRQHandler(void) { dma_irq_handler(DMA_STREAM_1_0); }
void __attribute__((used)) DMA1_Stream1_IRQHandler(void) { dma_irq_handler(DMA_STREAM_1_1); }
void __attribute__((used)) DMA1_Stream2_IRQHandler(void) { dma_irq_handler(DMA_STREAM_1_2); }
void __attribute__((used)) DMA1_Stream3_IRQHandler(void) { dma_irq_handler(DMA_STREAM_1_3); }
void __attribute__((used)) DMA1_Stream4_IRQHandler(void) { dma_irq_handler(DMA_STREAM_1_4); }
void __attribute__((used)) DMA1_Stream5_IRQHandler(void) { dma_irq_handler(DMA_STREAM_1_5); }
void __attribute__((used)) DMA1_Stream6_IRQHandler(void) { dma_irq_handler(DMA_STREAM_1_6); }
void __attribute__((used)) DMA1_Stream7_IRQHandler(void) { dma_irq_handler(DMA_STREAM_1_7); }

/*---------------------------------------------------------------------------
 * DMA2 stream ISR handlers
 *---------------------------------------------------------------------------*/
void __attribute__((used)) DMA2_Stream0_IRQHandler(void) { dma_irq_handler(DMA_STREAM_2_0); }
void __attribute__((used)) DMA2_Stream1_IRQHandler(void) { dma_irq_handler(DMA_STREAM_2_1); }
void __attribute__((used)) DMA2_Stream2_IRQHandler(void) { dma_irq_handler(DMA_STREAM_2_2); }
void __attribute__((used)) DMA2_Stream3_IRQHandler(void) { dma_irq_handler(DMA_STREAM_2_3); }
void __attribute__((used)) DMA2_Stream4_IRQHandler(void) { dma_irq_handler(DMA_STREAM_2_4); }
void __attribute__((used)) DMA2_Stream5_IRQHandler(void) { dma_irq_handler(DMA_STREAM_2_5); }
void __attribute__((used)) DMA2_Stream6_IRQHandler(void) { dma_irq_handler(DMA_STREAM_2_6); }
void __attribute__((used)) DMA2_Stream7_IRQHandler(void) { dma_irq_handler(DMA_STREAM_2_7); }
