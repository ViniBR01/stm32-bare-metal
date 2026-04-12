# Driver: UART

**Files:** `drivers/inc/uart.h`, `drivers/src/uart.c`

## Purpose

USART2 driver on the STM32F411RE. Used as the primary serial interface for the CLI and logging.

**Pins:** PA2 (TX), PA3 (RX) — connected to ST-Link virtual COM port on NUCLEO board.
**Baud rate:** 115200 (fixed at init time).

## Features

- **Blocking TX** (`uart_write`) — single character, CRLF conversion
- **DMA TX** (`uart_write_dma`) — non-blocking buffer transfer via DMA1 Stream 6
- **Interrupt RX** (`uart_register_rx_callback`) — per-character callback from USART2 IRQ
- **DMA RX** (`uart_start_rx_dma`) — circular DMA buffer; callback on IDLE line or DMA TC

## API

```c
void uart_init(void);

// Blocking TX
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

## Callback types

```c
typedef void (*uart_rx_callback_t)(char ch);
typedef void (*uart_tx_complete_callback_t)(void);
typedef void (*uart_rx_dma_callback_t)(uint8_t *data, uint16_t len);
```

## DMA assignments

| Direction | DMA | Stream | Channel |
|---|---|---|---|
| TX | DMA1 | Stream 6 | Channel 4 |
| RX | DMA1 | Stream 5 | Channel 4 |

## Usage notes

- When DMA RX is active, the per-character RXNE interrupt is disabled; `uart_rx_callback` is not called.
- `uart_write_dma` silently drops data if DMA TX is busy (no queuing).
- Used by `printf_dma` (`utils/src/printf_dma.c`) for non-blocking CLI output.
- Used by `log_platform` (`drivers/src/log_platform.c`) as the logging backend.
