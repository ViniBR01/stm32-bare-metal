#include <stdint.h>
#include <stddef.h>

#include "dma.h"
#include "gpio_handler.h"
#include "rcc.h"
#include "stm32f4xx.h"
#include "uart.h"

/* Register bit definitions - USART */
#define UART2EN                (1U<<17)
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

/* Configuration constants */
#define UART_BAUDRATE          115200

/* DMA stream/channel assignments (STM32F411 RM Table 28) */
#define UART_TX_DMA_STREAM     DMA_STREAM_1_6
#define UART_TX_DMA_CHANNEL    4
#define UART_RX_DMA_STREAM     DMA_STREAM_1_5
#define UART_RX_DMA_CHANNEL    4

/* Internal state variables */
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
static uint16_t compute_uart_bd(uint32_t periph_clk, uint32_t baudrate);
static void uart_set_baudrate(uint32_t periph_clk, uint32_t baudrate);
static void uart_tx_dma_init(void);
static void uart_nvic_init(void);
static void uart_tx_dma_tc_callback(dma_stream_id_t stream, void *ctx);

void uart_init(void) {
    /* Enable GPIOA clock and configure PA2 (TX) and PA3 (RX) */
    gpio_clock_enable(GPIO_PORT_A);
    gpio_configure_pin(GPIO_PORT_A, 2, GPIO_MODE_AF);
    gpio_configure_pin(GPIO_PORT_A, 3, GPIO_MODE_AF);
    
    /* Set PA2 alternate function to UART_TX (AF7) */
    GPIOA->AFR[0] &= ~(0xF<<8);  // Clear AF for PA2
    GPIOA->AFR[0] |= (7U<<8);    // Set AF7 (USART2_TX)
    
    /* Set PA3 alternate function to UART_RX (AF7) */
    GPIOA->AFR[0] &= ~(0xF<<12); // Clear AF for PA3
    GPIOA->AFR[0] |= (7U<<12);   // Set AF7 (USART2_RX)
    
    /* Enable UART2 clock */
    RCC->APB1ENR |= UART2EN;
    
    /* Configure baudrate */
    uart_set_baudrate(rcc_get_apb1_clk(), UART_BAUDRATE);
    
    /* Enable transmitter, receiver, and UART module */
    USART2->CR1 |= CR1_TE;
    USART2->CR1 |= CR1_RE;
    USART2->CR1 |= CR1_UE;
    
    /* Initialize DMA for TX via generic DMA driver */
    uart_tx_dma_init();
    
    /* Enable UART DMA mode for transmitter */
    USART2->CR3 |= CR3_DMAT;
    
    /* Enable UART interrupts for RX and errors */
    USART2->CR1 |= CR1_RXNEIE;  // RX not empty interrupt
    USART2->CR1 |= CR1_IDLEIE;  // Idle line interrupt
    USART2->CR3 |= CR3_EIE;     // Error interrupt
    
    /* Configure NVIC for UART interrupt (DMA NVIC handled by dma driver) */
    uart_nvic_init();
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
    dma_stream_start(UART_TX_DMA_STREAM, (uint32_t)data, length);
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
    /* In circular mode the DMA wraps around automatically.
       Deliver any data accumulated since the last delivery. */
    if (!rx_dma_active || !rx_dma_callback) return;

    uint16_t ndtr = dma_stream_get_ndtr(UART_RX_DMA_STREAM);
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
    if (!buf || size == 0) return;

    rx_dma_buf       = buf;
    rx_dma_buf_size  = size;
    rx_dma_last_ndtr = size;
    rx_dma_active    = 1;

    /* Disable per-character RXNE interrupt -- DMA handles reception now */
    USART2->CR1 &= ~CR1_RXNEIE;

    /* Configure DMA stream for USART2_RX: circular, P2M, MINC */
    dma_stream_config_t rx_cfg = {
        .stream        = UART_RX_DMA_STREAM,
        .channel       = UART_RX_DMA_CHANNEL,
        .direction     = DMA_DIR_PERIPH_TO_MEM,
        .periph_addr   = (uint32_t)&(USART2->DR),
        .mem_inc       = 1,
        .periph_inc    = 0,
        .circular      = 1,
        .priority      = DMA_PRIO_HIGH,
        .tc_callback   = uart_rx_dma_tc_callback,
        .error_callback = NULL,
        .cb_ctx        = NULL,
        .nvic_priority = 1,
    };
    dma_stream_init(&rx_cfg);

    /* Enable USART DMA receiver */
    USART2->CR3 |= CR3_DMAR;

    /* Start DMA reception */
    dma_stream_start(UART_RX_DMA_STREAM, (uint32_t)buf, size);
}

void uart_stop_rx_dma(void) {
    if (!rx_dma_active) return;

    rx_dma_active = 0;

    /* Stop DMA stream and release */
    dma_stream_stop(UART_RX_DMA_STREAM);
    dma_stream_release(UART_RX_DMA_STREAM);

    /* Disable USART DMA receiver */
    USART2->CR3 &= ~CR3_DMAR;

    /* Re-enable per-character RXNE interrupt */
    USART2->CR1 |= CR1_RXNEIE;
}

/* Interrupt handler -- __attribute__((used)) prevents LTO from stripping
   this strong definition before the linker resolves the weak vector-table alias */

void __attribute__((used)) USART2_IRQHandler(void) {
    uint32_t sr = USART2->SR;
    
    /* Handle RX not empty (only when not using DMA RX) */
    if ((sr & SR_RXNE) && !rx_dma_active) {
        char ch = (char)(USART2->DR & 0xFF);
        if (rx_callback != NULL) {
            rx_callback(ch);
        }
    }
    
    /* Handle idle line detection */
    if (sr & SR_IDLE) {
        /* Clear IDLE flag by reading SR then DR */
        (void)USART2->DR;

        /* When DMA RX is active, deliver received bytes on idle */
        if (rx_dma_active && rx_dma_callback) {
            uint16_t ndtr = dma_stream_get_ndtr(UART_RX_DMA_STREAM);
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
    }
    
    /* Handle overrun error */
    if (sr & SR_ORE) {
        error_flags.overrun_error = 1;
        /* Clear ORE by reading SR then DR */
        (void)USART2->DR;
    }
    
    /* Handle framing error */
    if (sr & SR_FE) {
        error_flags.framing_error = 1;
        /* Clear FE by reading SR then DR */
        (void)USART2->DR;
    }
    
    /* Handle noise error */
    if (sr & SR_NF) {
        error_flags.noise_error = 1;
        /* Clear NF by reading SR then DR */
        (void)USART2->DR;
    }
}

/* Private function implementations */

static uint16_t compute_uart_bd(uint32_t periph_clk, uint32_t baudrate) {
    return ((periph_clk + (baudrate / 2U)) / baudrate);
}

static void uart_set_baudrate(uint32_t periph_clk, uint32_t baudrate) {
    USART2->BRR = compute_uart_bd(periph_clk, baudrate);
}

/**
 * @brief DMA TX transfer-complete callback (called from DMA ISR context)
 */
static void uart_tx_dma_tc_callback(dma_stream_id_t stream, void *ctx) {
    (void)stream;
    (void)ctx;

    /* Mark TX as not busy */
    tx_busy = 0;

    /* Call user callback if registered */
    if (tx_complete_callback != NULL) {
        tx_complete_callback();
    }
}

/**
 * @brief Initialize DMA for UART TX using the generic DMA driver
 */
static void uart_tx_dma_init(void) {
    dma_stream_config_t tx_cfg = {
        .stream        = UART_TX_DMA_STREAM,
        .channel       = UART_TX_DMA_CHANNEL,
        .direction     = DMA_DIR_MEM_TO_PERIPH,
        .periph_addr   = (uint32_t)&(USART2->DR),
        .mem_inc       = 1,
        .periph_inc    = 0,
        .circular      = 0,
        .priority      = DMA_PRIO_HIGH,
        .tc_callback   = uart_tx_dma_tc_callback,
        .error_callback = NULL,
        .cb_ctx        = NULL,
        .nvic_priority = 0,  /* DMA higher priority than UART (2) */
    };
    dma_stream_init(&tx_cfg);
}

static void uart_nvic_init(void) {
    /* Enable USART2 interrupt in NVIC */
    NVIC_EnableIRQ(USART2_IRQn);
    NVIC_SetPriority(USART2_IRQn, 2);  // Lower priority (higher number)
    // DMA NVIC is configured by dma_stream_init() with priority 0
    // DMA must have higher priority than UART to allow DMA completion 
    // interrupts to fire even when called from UART interrupt context
}
