#include <stdint.h>
#include <stddef.h>

#include "dma.h"
#include "gpio_handler.h"
#include "irq_priorities.h"
#include "rcc.h"
#include "stm32f4xx.h"
#include "uart.h"
#include "uart_calc.h"

/* Register bit definitions - USART */
#define CR1_RE                 (1U<<2)
#define CR1_TE                 (1U<<3)
#define CR1_UE                 (1U<<13)
#define CR1_RXNEIE             (1U<<5)
#define CR1_TCIE               (1U<<6)
#define CR1_IDLEIE             (1U<<4)
#define CR3_DMAT               (1U<<7)
#define CR3_DMAR               (1U<<6)
#define CR3_EIE                (1U<<0)
#define SR_TXE                 (1U<<7)
#define SR_RXNE                (1U<<5)
#define SR_TC                  (1U<<6)
#define SR_IDLE                (1U<<4)
#define SR_ORE                 (1U<<3)
#define SR_NF                  (1U<<2)
#define SR_FE                  (1U<<1)

/*===========================================================================
 * Hardware descriptor table
 *
 * Per-instance constants: registers, RCC enable, GPIO pins, DMA streams,
 * and APB clock getter function.
 *
 * DMA stream/channel assignments (STM32F411 RM Table 28):
 *   USART1 TX: DMA2 Stream 7, Channel 4
 *   USART1 RX: DMA2 Stream 2, Channel 4
 *   USART2 TX: DMA1 Stream 6, Channel 4
 *   USART2 RX: DMA1 Stream 5, Channel 4
 *   USART6 TX: DMA2 Stream 6, Channel 5
 *   USART6 RX: DMA2 Stream 1, Channel 5
 *===========================================================================*/

typedef uint32_t (*clk_getter_t)(void);

typedef struct {
    USART_TypeDef      *regs;
    volatile uint32_t  *rcc_enr;        /* pointer to APB enable register */
    uint32_t            rcc_en_bit;     /* bit to set in *rcc_enr */
    /* GPIO TX */
    gpio_port_t         tx_port;
    uint8_t             tx_pin;
    uint8_t             tx_af;
    /* GPIO RX */
    gpio_port_t         rx_port;
    uint8_t             rx_pin;
    uint8_t             rx_af;
    /* DMA */
    dma_stream_id_t     tx_dma_stream;
    uint8_t             tx_dma_channel;
    dma_stream_id_t     rx_dma_stream;
    uint8_t             rx_dma_channel;
    /* NVIC */
    IRQn_Type           irqn;
    /* APB clock query function */
    clk_getter_t        get_periph_clk;
} uart_hw_info_t;

static const uart_hw_info_t uart_hw_table[UART_INSTANCE_COUNT] = {
    [UART_INSTANCE_1] = {
        .regs           = USART1,
        .rcc_enr        = &RCC->APB2ENR,
        .rcc_en_bit     = RCC_APB2ENR_USART1EN,
        .tx_port        = GPIO_PORT_A,
        .tx_pin         = 9,
        .tx_af          = 7,
        .rx_port        = GPIO_PORT_B,
        .rx_pin         = 7,
        .rx_af          = 7,
        .tx_dma_stream  = DMA_STREAM_2_7,
        .tx_dma_channel = 4,
        .rx_dma_stream  = DMA_STREAM_2_2,
        .rx_dma_channel = 4,
        .irqn           = USART1_IRQn,
        .get_periph_clk = rcc_get_apb2_clk,
    },
    [UART_INSTANCE_2] = {
        .regs           = USART2,
        .rcc_enr        = &RCC->APB1ENR,
        .rcc_en_bit     = RCC_APB1ENR_USART2EN,
        .tx_port        = GPIO_PORT_A,
        .tx_pin         = 2,
        .tx_af          = 7,
        .rx_port        = GPIO_PORT_A,
        .rx_pin         = 3,
        .rx_af          = 7,
        .tx_dma_stream  = DMA_STREAM_1_6,
        .tx_dma_channel = 4,
        .rx_dma_stream  = DMA_STREAM_1_5,
        .rx_dma_channel = 4,
        .irqn           = USART2_IRQn,
        .get_periph_clk = rcc_get_apb1_clk,
    },
    [UART_INSTANCE_6] = {
        .regs           = USART6,
        .rcc_enr        = &RCC->APB2ENR,
        .rcc_en_bit     = RCC_APB2ENR_USART6EN,
        .tx_port        = GPIO_PORT_C,
        .tx_pin         = 6,
        .tx_af          = 8,
        .rx_port        = GPIO_PORT_C,
        .rx_pin         = 7,
        .rx_af          = 8,
        .tx_dma_stream  = DMA_STREAM_2_6,
        .tx_dma_channel = 5,
        .rx_dma_stream  = DMA_STREAM_2_1,
        .rx_dma_channel = 5,
        .irqn           = USART6_IRQn,
        .get_periph_clk = rcc_get_apb2_clk,
    },
};

/*===========================================================================
 * Internal state variables
 *
 * The driver currently supports a single active instance at a time for the
 * polling/DMA TX/RX and callback paths (USART2 by default via uart_init()).
 * uart_init_config() stores a pointer to the active instance's hw entry so
 * the IRQ handlers and DMA callbacks can find the right registers.
 *===========================================================================*/

static const uart_hw_info_t *active_hw = NULL;  /* set by uart_init_config() */

static volatile uint8_t tx_busy = 0;
static uart_tx_complete_callback_t tx_complete_callback = NULL;
static uart_rx_callback_t rx_callback = NULL;
static volatile uart_error_flags_t error_flags = {0};

/* DMA RX state */
static uart_rx_dma_callback_t rx_dma_callback = NULL;
static uint8_t *rx_dma_buf = NULL;
static uint16_t rx_dma_buf_size = 0;
static volatile uint16_t rx_dma_last_ndtr = 0;
static volatile uint8_t  rx_dma_active = 0;

/* Private function prototypes */
static void uart_tx_dma_init(const uart_hw_info_t *hw);
static void uart_rx_dma_tc_callback(dma_stream_id_t stream, void *ctx);
static void uart_tx_dma_tc_callback(dma_stream_id_t stream, void *ctx);

/*===========================================================================
 * Public API
 *===========================================================================*/

void uart_init(void) {
    /* Backward-compatible wrapper: USART2 at 115200 baud */
    static const uart_config_t default_cfg = {
        .instance  = UART_INSTANCE_2,
        .baud_rate = 115200U,
    };
    uart_init_config(&default_cfg);
}

err_t uart_init_config(const uart_config_t *cfg) {
    if (!cfg) return ERR_INVALID_ARG;
    if (cfg->instance >= UART_INSTANCE_COUNT) return ERR_INVALID_ARG;

    const uart_hw_info_t *hw = &uart_hw_table[cfg->instance];
    active_hw = hw;

    /* Enable GPIO clocks and configure TX/RX pins in AF mode */
    gpio_clock_enable(hw->tx_port);
    gpio_clock_enable(hw->rx_port);
    gpio_configure_pin(hw->tx_port, hw->tx_pin, GPIO_MODE_AF);
    gpio_configure_pin(hw->rx_port, hw->rx_pin, GPIO_MODE_AF);
    gpio_set_af(hw->tx_port, hw->tx_pin, hw->tx_af);
    gpio_set_af(hw->rx_port, hw->rx_pin, hw->rx_af);

    /* Enable USART peripheral clock */
    *hw->rcc_enr |= hw->rcc_en_bit;

    /* Configure baud rate */
    hw->regs->BRR = uart_compute_baud_divisor(hw->get_periph_clk(), cfg->baud_rate);

    /* Enable transmitter, receiver, and UART module */
    hw->regs->CR1 |= CR1_TE;
    hw->regs->CR1 |= CR1_RE;
    hw->regs->CR1 |= CR1_UE;

    /* Initialize DMA for TX via generic DMA driver */
    uart_tx_dma_init(hw);

    /* Enable UART DMA mode for transmitter */
    hw->regs->CR3 |= CR3_DMAT;

    /* Enable UART interrupts for RX and errors */
    hw->regs->CR1 |= CR1_RXNEIE;  /* RX not empty interrupt */
    hw->regs->CR1 |= CR1_IDLEIE;  /* Idle line interrupt */
    hw->regs->CR3 |= CR3_EIE;     /* Error interrupt */

    /* Configure NVIC for UART interrupt */
    NVIC_SetPriority(hw->irqn, IRQ_PRIO_UART);
    NVIC_EnableIRQ(hw->irqn);

    return ERR_OK;
}

char uart_read(void) {
    /* Wait until RXNE (Read data register not empty) flag is set */
    while (!(USART2->SR & SR_RXNE));

    /* Read and return the received character */
    return (char)(USART2->DR & 0xFF);
}

void uart_write(char ch) {
    /* If sending a newline, transmit carriage return first for proper
       terminal display (LF -> CRLF conversion) */
    if (ch == '\n') {
        while (!(USART2->SR & SR_TXE));
        USART2->DR = ('\r' & 0xFF);
    }

    /* Wait until transmit data register is empty */
    while (!(USART2->SR & SR_TXE));

    /* Write character to transmit data register */
    USART2->DR = (ch & 0xFF);
}

void uart_write_dma(const char* data, uint16_t length) {
    /* Check if DMA is already busy */
    if (tx_busy || length == 0) {
        return;
    }

    /* Mark TX as busy */
    tx_busy = 1;

    /* Start DMA transfer via generic driver */
    if (active_hw) {
        dma_stream_start(active_hw->tx_dma_stream, (uint32_t)data, length);
    }
}

void uart_register_rx_callback(uart_rx_callback_t callback) {
    rx_callback = callback;
}

void uart_register_tx_complete_callback(uart_tx_complete_callback_t callback) {
    tx_complete_callback = callback;
}

void uart_register_rx_dma_callback(uart_rx_dma_callback_t callback) {
    rx_dma_callback = callback;
}

uart_error_flags_t uart_get_errors(void) {
    return error_flags;
}

void uart_clear_errors(void) {
    error_flags.overrun_error = 0;
    error_flags.framing_error = 0;
    error_flags.noise_error = 0;
}

uint8_t uart_is_tx_busy(void) {
    return tx_busy;
}

/*===========================================================================
 * DMA RX support -- continuous reception with idle-line detection
 *===========================================================================*/

static void uart_rx_dma_tc_callback(dma_stream_id_t stream, void *ctx) {
    (void)stream;
    (void)ctx;
    if (!rx_dma_active || !rx_dma_callback || !active_hw) return;

    uint16_t ndtr = dma_stream_get_ndtr(active_hw->rx_dma_stream);
    uint16_t head = rx_dma_buf_size - ndtr;
    uint16_t tail = rx_dma_buf_size - rx_dma_last_ndtr;

    if (head != tail) {
        if (head > tail) {
            rx_dma_callback(&rx_dma_buf[tail], head - tail);
        } else {
            /* Wrapped: deliver tail..end, then 0..head */
            if (tail < rx_dma_buf_size) {
                rx_dma_callback(&rx_dma_buf[tail], rx_dma_buf_size - tail);
            }
            if (head > 0) {
                rx_dma_callback(&rx_dma_buf[0], head);
            }
        }
    }
    rx_dma_last_ndtr = ndtr;
}

void uart_start_rx_dma(uint8_t *buf, uint16_t size) {
    if (!buf || size == 0 || !active_hw) return;

    rx_dma_buf       = buf;
    rx_dma_buf_size  = size;
    rx_dma_last_ndtr = size;
    rx_dma_active    = 1;

    /* Disable per-character RXNE interrupt -- DMA handles reception now */
    active_hw->regs->CR1 &= ~CR1_RXNEIE;

    /* Configure DMA stream for RX: circular, P2M, MINC */
    dma_stream_config_t rx_cfg = {
        .stream         = active_hw->rx_dma_stream,
        .channel        = active_hw->rx_dma_channel,
        .direction      = DMA_DIR_PERIPH_TO_MEM,
        .periph_addr    = (uint32_t)&(active_hw->regs->DR),
        .mem_inc        = 1,
        .periph_inc     = 0,
        .circular       = 1,
        .priority       = DMA_PRIO_HIGH,
        .tc_callback    = uart_rx_dma_tc_callback,
        .error_callback = NULL,
        .cb_ctx         = NULL,
        .nvic_priority  = IRQ_PRIO_DMA_LOW,
    };
    dma_stream_init(&rx_cfg);

    /* Enable USART DMA receiver */
    active_hw->regs->CR3 |= CR3_DMAR;

    /* Start DMA reception */
    dma_stream_start(active_hw->rx_dma_stream, (uint32_t)buf, size);
}

void uart_stop_rx_dma(void) {
    if (!rx_dma_active || !active_hw) return;

    rx_dma_active = 0;

    /* Stop DMA stream and release */
    dma_stream_stop(active_hw->rx_dma_stream);
    dma_stream_release(active_hw->rx_dma_stream);

    /* Disable USART DMA receiver */
    active_hw->regs->CR3 &= ~CR3_DMAR;

    /* Re-enable per-character RXNE interrupt */
    active_hw->regs->CR1 |= CR1_RXNEIE;
}

/*===========================================================================
 * Interrupt handlers
 *
 * __attribute__((used)) prevents LTO from stripping these strong definitions
 * before the linker resolves the weak vector-table aliases.
 *
 * Each handler dispatches through the shared rx_callback / rx_dma_callback
 * state. When only one UART instance is active at a time (the common case),
 * this is equivalent to the previous single-instance design.
 *===========================================================================*/

/* Shared handler body — called from each USART ISR */
static void uart_irq_handler_common(USART_TypeDef *regs,
                                    dma_stream_id_t rx_stream) {
    uint32_t sr = regs->SR;

    /* Handle RX not empty (only when not using DMA RX) */
    if ((sr & SR_RXNE) && !rx_dma_active) {
        char ch = (char)(regs->DR & 0xFF);
        if (rx_callback != NULL) {
            rx_callback(ch);
        }
    }

    /* Handle idle line detection */
    if (sr & SR_IDLE) {
        /* Clear IDLE flag by reading SR then DR */
        (void)regs->DR;

        /* When DMA RX is active, deliver received bytes on idle */
        if (rx_dma_active && rx_dma_callback) {
            uint16_t ndtr = dma_stream_get_ndtr(rx_stream);
            uint16_t head = rx_dma_buf_size - ndtr;
            uint16_t tail = rx_dma_buf_size - rx_dma_last_ndtr;

            if (head != tail) {
                if (head > tail) {
                    rx_dma_callback(&rx_dma_buf[tail], head - tail);
                } else {
                    if (tail < rx_dma_buf_size) {
                        rx_dma_callback(&rx_dma_buf[tail], rx_dma_buf_size - tail);
                    }
                    if (head > 0) {
                        rx_dma_callback(&rx_dma_buf[0], head);
                    }
                }
            }
            rx_dma_last_ndtr = ndtr;
        }
    }

    /* Handle overrun error */
    if (sr & SR_ORE) {
        error_flags.overrun_error = 1;
        (void)regs->DR;
    }

    /* Handle framing error */
    if (sr & SR_FE) {
        error_flags.framing_error = 1;
        (void)regs->DR;
    }

    /* Handle noise error */
    if (sr & SR_NF) {
        error_flags.noise_error = 1;
        (void)regs->DR;
    }
}

void __attribute__((used)) USART1_IRQHandler(void) {
    uart_irq_handler_common(USART1, DMA_STREAM_2_2);
}

void __attribute__((used)) USART2_IRQHandler(void) {
    uart_irq_handler_common(USART2, DMA_STREAM_1_5);
}

void __attribute__((used)) USART6_IRQHandler(void) {
    uart_irq_handler_common(USART6, DMA_STREAM_2_1);
}

/*===========================================================================
 * Pure calculation functions — also declared in uart_calc.h for testing
 *===========================================================================*/

uint16_t uart_compute_baud_divisor(uint32_t periph_clk, uint32_t baudrate) {
    return (uint16_t)((periph_clk + (baudrate / 2U)) / baudrate);
}

uint16_t uart_circ_bytes_available(uint16_t ndtr, uint16_t last_ndtr,
                                   uint16_t buf_size) {
    uint16_t head = buf_size - ndtr;
    uint16_t tail = buf_size - last_ndtr;
    if (head >= tail) {
        return head - tail;
    }
    return (buf_size - tail) + head;
}

/*===========================================================================
 * Private helpers
 *===========================================================================*/

/**
 * @brief DMA TX transfer-complete callback (called from DMA ISR context)
 */
static void uart_tx_dma_tc_callback(dma_stream_id_t stream, void *ctx) {
    (void)stream;
    (void)ctx;

    tx_busy = 0;

    if (tx_complete_callback != NULL) {
        tx_complete_callback();
    }
}

/**
 * @brief Initialize DMA for UART TX using the generic DMA driver
 */
static void uart_tx_dma_init(const uart_hw_info_t *hw) {
    dma_stream_config_t tx_cfg = {
        .stream         = hw->tx_dma_stream,
        .channel        = hw->tx_dma_channel,
        .direction      = DMA_DIR_MEM_TO_PERIPH,
        .periph_addr    = (uint32_t)&(hw->regs->DR),
        .mem_inc        = 1,
        .periph_inc     = 0,
        .circular       = 0,
        .priority       = DMA_PRIO_HIGH,
        .tc_callback    = uart_tx_dma_tc_callback,
        .error_callback = NULL,
        .cb_ctx         = NULL,
        .nvic_priority  = IRQ_PRIO_DMA_HIGH,
    };
    dma_stream_init(&tx_cfg);
}
