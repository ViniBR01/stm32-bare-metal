# Driver: UART

**Files:** `drivers/inc/uart.h`, `drivers/src/uart.c`

## Purpose

Multi-instance USART driver for the STM32F411RE. Supports USART1, USART2, and USART6
with configurable baud rates. USART2 is the primary serial interface connected to the
ST-Link virtual COM port on the NUCLEO board.

## Supported instances

| Instance       | APB bus | Freq @ 100 MHz SYSCLK | TX pin | RX pin | AF |
|----------------|---------|------------------------|--------|--------|----|
| UART_INSTANCE_1| APB2    | 100 MHz                | PA9    | PB7    | 7  |
| UART_INSTANCE_2| APB1    | 50 MHz                 | PA2    | PA3    | 7  |
| UART_INSTANCE_6| APB2    | 100 MHz                | PC6    | PC7    | 8  |

USART2 (PA2/PA3) is the default — connected to the ST-Link virtual COM port on the
NUCLEO board and used by the CLI and logging infrastructure.

USART1 (PA9/PB7) and USART6 (PC6/PC7) are wired as a loopback pair on the NUCLEO
board HIL test fixture.

## Features

- **Multi-instance**: USART1, USART2, USART6 via `uart_init_config()`
- **Configurable baud rate**: computed from the actual APB clock using `uart_compute_baud_divisor()`
- **Backward compatible**: `uart_init()` initialises USART2 at 115200 baud, no API change
- **Blocking TX** (`uart_write`) — single character, CRLF conversion
- **DMA TX** (`uart_write_dma`) — non-blocking buffer transfer
- **Interrupt RX** (`uart_register_rx_callback`) — per-character callback from USART IRQ
- **DMA RX** (`uart_start_rx_dma`) — circular DMA buffer; callback on IDLE line or DMA TC

## API

```c
/* Instance + baud rate selection */
typedef enum {
    UART_INSTANCE_1 = 0,  /* USART1 — APB2, PA9/PB7, AF7 */
    UART_INSTANCE_2 = 1,  /* USART2 — APB1, PA2/PA3, AF7 (default) */
    UART_INSTANCE_6 = 2,  /* USART6 — APB2, PC6/PC7, AF8 */
} uart_instance_t;

typedef struct {
    uart_instance_t instance;
    uint32_t        baud_rate;   /* e.g. 115200 */
} uart_config_t;

/* Initialisation */
void  uart_init(void);                             /* USART2 at 115200 — backward compat */
err_t uart_init_config(const uart_config_t *cfg);  /* any instance, any baud rate */

// Blocking TX/RX (USART2 / default instance)
char uart_read(void);
void uart_write(char ch);                          // '\n' → "\r\n"

// DMA TX (non-blocking)
void    uart_write_dma(const char *data, uint16_t length);
uint8_t uart_is_tx_busy(void);

// Callbacks
void uart_register_rx_callback(uart_rx_callback_t callback);           // per-char ISR RX
void uart_register_tx_complete_callback(uart_tx_complete_callback_t callback);

// DMA RX (block reception)
void uart_start_rx_dma(uint8_t *buf, uint16_t size);  // circular DMA
void uart_stop_rx_dma(void);
void uart_register_rx_dma_callback(uart_rx_dma_callback_t callback);  // called on IDLE/TC

// Error handling
uart_error_flags_t uart_get_errors(void);
void               uart_clear_errors(void);
```

## Usage examples

```c
/* Default — USART2 at 115200 (same as before) */
uart_init();

/* USART1 at 9600 baud */
uart_config_t cfg1 = { .instance = UART_INSTANCE_1, .baud_rate = 9600U };
uart_init_config(&cfg1);

/* USART6 at 115200 baud */
uart_config_t cfg6 = { .instance = UART_INSTANCE_6, .baud_rate = 115200U };
uart_init_config(&cfg6);
```

## Callback types

```c
typedef void (*uart_rx_callback_t)(char ch);
typedef void (*uart_tx_complete_callback_t)(void);
typedef void (*uart_rx_dma_callback_t)(uint8_t *data, uint16_t len);
```

## DMA assignments

| Instance | Direction | DMA  | Stream | Channel |
|----------|-----------|------|--------|---------|
| USART1   | TX        | DMA2 | 7      | 4       |
| USART1   | RX        | DMA2 | 2      | 4       |
| USART2   | TX        | DMA1 | 6      | 4       |
| USART2   | RX        | DMA1 | 5      | 4       |
| USART6   | TX        | DMA2 | 6      | 5       |
| USART6   | RX        | DMA2 | 1      | 5       |

## Implementation notes

- A hardware descriptor table (`uart_hw_table[UART_INSTANCE_COUNT]`) maps each
  instance to its registers, RCC enable bit, GPIO pins, DMA streams, IRQn, and APB
  clock getter function — same pattern as the SPI driver.
- USART1 and USART6 are on APB2 (100 MHz at SYSCLK=100 MHz); USART2 is on APB1
  (50 MHz). The baud divisor is computed via `rcc_get_apb1_clk()` or
  `rcc_get_apb2_clk()` as appropriate, automatically.
- Only one UART instance is "active" at a time for the DMA RX / callback paths.
  `uart_init_config()` sets the active instance.
- When DMA RX is active, the per-character RXNE interrupt is disabled; `uart_rx_callback`
  is not called.
- `uart_write_dma` silently drops data if DMA TX is busy (no queuing).
- Used by `printf_dma` (`utils/src/printf_dma.c`) for non-blocking CLI output.
- Used by `log_platform` (`drivers/src/log_platform.c`) as the logging backend.
