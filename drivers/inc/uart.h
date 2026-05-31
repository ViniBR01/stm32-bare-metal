#ifndef UART_H_
#define UART_H_

#include <stdint.h>
#include "error.h"

/**
 * @brief Callback function type for receiving characters (interrupt-driven)
 *
 * @param ch The received character
 */
typedef void (*uart_rx_callback_t)(char ch);

/**
 * @brief Callback function type for TX complete notification
 */
typedef void (*uart_tx_complete_callback_t)(void);

/**
 * @brief Callback function type for DMA block reception
 *
 * Called from ISR context (IDLE line or DMA TC) with a pointer into the
 * DMA receive buffer and the number of newly received bytes.
 *
 * @param data  Pointer to the first new byte in the receive buffer
 * @param len   Number of new bytes available
 */
typedef void (*uart_rx_dma_callback_t)(uint8_t *data, uint16_t len);

/**
 * @brief UART error flags structure
 */
typedef struct {
    uint8_t overrun_error;   /**< Overrun error occurred */
    uint8_t framing_error;   /**< Framing error occurred */
    uint8_t noise_error;     /**< Noise error occurred */
} uart_error_flags_t;

/**
 * @brief UART instance selector
 *
 * Selects which USART peripheral to initialize.
 *
 * | Instance       | APB bus | TX pin   | RX pin   | AF  |
 * |----------------|---------|----------|----------|-----|
 * | UART_INSTANCE_1| APB2    | PA9/AF7  | PB7/AF7  |  7  |
 * | UART_INSTANCE_2| APB1    | PA2/AF7  | PA3/AF7  |  7  |
 * | UART_INSTANCE_6| APB2    | PC6/AF8  | PC7/AF8  |  8  |
 */
typedef enum {
    UART_INSTANCE_1 = 0,  /**< USART1 — APB2 (100 MHz), TX=PA9/AF7, RX=PB7/AF7 */
    UART_INSTANCE_2 = 1,  /**< USART2 — APB1 (50 MHz),  TX=PA2/AF7, RX=PA3/AF7 */
    UART_INSTANCE_6 = 2,  /**< USART6 — APB2 (100 MHz), TX=PC6/AF8, RX=PC7/AF8 */
    UART_INSTANCE_COUNT
} uart_instance_t;

/**
 * @brief UART initialization configuration
 */
typedef struct {
    uart_instance_t instance;  /**< Which USART peripheral to use */
    uint32_t        baud_rate; /**< Baud rate in bps (e.g. 115200) */
} uart_config_t;

/**
 * @brief Initialize USART2 with default settings (115200 baud, PA2/PA3).
 *
 * Convenience wrapper for existing code. Equivalent to calling
 * uart_init_config() with UART_INSTANCE_2 at 115200 baud.
 *
 * Configures USART2 on PA2 (TX) and PA3 (RX) with 115200 baud rate.
 * Enables both transmit and receive functionality.
 * Sets up DMA for TX (via generic DMA driver) and enables RXNE interrupt
 * for RX.
 */
void uart_init(void);

/**
 * @brief Tear down the currently-initialised UART instance.
 *
 * Disables the USART (CR1/CR2/CR3 cleared) and the matching NVIC entry,
 * clears any pending interrupt, and resets driver-level state (active
 * instance, callbacks, DMA buffers, error flags) so the next
 * uart_init_config call starts clean.
 *
 * Designed for the bootloader -> app handover: the bootloader prints
 * boot logs over USART2 (which arms RXNEIE + the NVIC entry), then
 * calls uart_deinit() before jumping so a stray RX byte cannot raise
 * USART2_IRQHandler against the app's not-yet-initialised state.
 *
 * Safe to call when no instance has been initialised — returns
 * immediately.
 */
void uart_deinit(void);

/**
 * @brief Initialize a UART instance with configurable parameters.
 *
 * Configures the specified USART instance at the requested baud rate.
 * GPIO pins, DMA streams, clock sources, and NVIC entries are selected
 * automatically based on the instance.
 *
 * @param cfg  Pointer to configuration struct; must not be NULL.
 * @return ERR_OK on success, ERR_INVALID_ARG if cfg is NULL or instance is invalid.
 */
err_t uart_init_config(const uart_config_t *cfg);

/**
 * @brief Read a single character from UART (blocking)
 *
 * Waits until a character is received on UART RX.
 * Reads from USART2 (the default instance).
 *
 * @return The received character
 */
char uart_read(void);

/**
 * @brief Write a single character to UART (blocking)
 *
 * Transmits a character over UART TX. Automatically converts '\n' to "\r\n"
 * for proper terminal display.
 * Writes to USART2 (the default instance).
 *
 * @param ch Character to transmit
 */
void uart_write(char ch);

/**
 * @brief Write a NUL-terminated string to UART (blocking, byte-by-byte).
 *
 * Convenience helper for printf-free contexts (e.g. the bootloader, where
 * the sector-0 budget rules out pulling in printf_dma).  Stops at the
 * first '\0'.  Each byte goes through uart_write(), so '\n' still
 * gets the CRLF conversion.
 *
 * @param s NUL-terminated string; passing NULL is undefined behaviour.
 */
void uart_puts(const char *s);

/**
 * @brief Print a uint32_t as "0xNNNNNNNN" using uart_puts under the hood.
 *
 * Always prints exactly 10 characters (the "0x" prefix plus 8 uppercase
 * hex digits).  No format-string machinery — safe to use from the
 * bootloader.
 */
void uart_print_hex32(uint32_t v);

/**
 * @brief Print a uint32_t in base-10 (no leading zeros, no sign, no width).
 *
 * Stack-only, no allocations, no printf dependency.  Used by the
 * bootloader's verify-time log line.
 */
void uart_print_dec32(uint32_t v);

/**
 * @brief Write data to UART using DMA (non-blocking)
 *
 * Transmits a buffer over UART TX using DMA. Returns immediately if DMA
 * is busy with a previous transfer. Does NOT perform CRLF conversion.
 * Uses the DMA TX stream of the default USART2 instance.
 *
 * @param data Pointer to data buffer to transmit
 * @param length Number of bytes to transmit
 */
void uart_write_dma(const char* data, uint16_t length);

/**
 * @brief Register a callback for received characters (interrupt-driven RX)
 *
 * The callback will be invoked from the USART2 interrupt handler
 * when a character is received. Not used when DMA RX is active.
 *
 * @param callback Function to call when character is received (NULL to disable)
 */
void uart_register_rx_callback(uart_rx_callback_t callback);

/**
 * @brief Register a callback for TX complete notification
 *
 * The callback will be invoked from the DMA interrupt handler
 * when transmission is complete.
 *
 * @param callback Function to call when TX is complete (NULL to disable)
 */
void uart_register_tx_complete_callback(uart_tx_complete_callback_t callback);

/**
 * @brief Register a callback for DMA block reception
 *
 * The callback will be invoked from ISR context (USART IDLE line or DMA
 * transfer-complete) whenever new data has been received into the DMA
 * receive buffer.
 *
 * @param callback Function to call with received data (NULL to disable)
 */
void uart_register_rx_dma_callback(uart_rx_dma_callback_t callback);

/**
 * @brief Get current error flags
 *
 * @return Structure containing current error flags
 */
uart_error_flags_t uart_get_errors(void);

/**
 * @brief Clear all error flags
 */
void uart_clear_errors(void);

/**
 * @brief Check if DMA transmission is in progress
 *
 * @return 1 if DMA TX is busy, 0 if idle
 */
uint8_t uart_is_tx_busy(void);

/**
 * @brief Start continuous DMA reception on UART RX
 *
 * Configures the USART2 RX DMA stream in circular mode to continuously
 * receive data into the provided buffer. The registered rx_dma_callback
 * is called on USART IDLE line events and DMA transfer-complete events
 * with pointers to newly received data.
 *
 * While DMA RX is active, the per-character RXNE interrupt is disabled
 * and the uart_rx_callback is not called.
 *
 * @param buf   Caller-allocated receive buffer
 * @param size  Size of the receive buffer in bytes
 */
void uart_start_rx_dma(uint8_t *buf, uint16_t size);

/**
 * @brief Stop DMA reception on UART RX
 *
 * Stops the DMA stream, disables the USART DMA receiver, and re-enables
 * the per-character RXNE interrupt.
 */
void uart_stop_rx_dma(void);

#endif /* UART_H_ */
