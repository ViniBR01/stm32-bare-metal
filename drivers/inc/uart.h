#ifndef UART_H_
#define UART_H_

#include <stdint.h>

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
 * @brief Initialize UART2 for communication
 * 
 * Configures USART2 on PA2 (TX) and PA3 (RX) with 115200 baud rate.
 * Enables both transmit and receive functionality.
 * Sets up DMA for TX (via generic DMA driver) and enables RXNE interrupt
 * for RX.
 */
void uart_init(void);

/**
 * @brief Read a single character from UART (blocking)
 * 
 * Waits until a character is received on UART RX.
 * 
 * @return The received character
 */
char uart_read(void);

/**
 * @brief Write a single character to UART (blocking)
 * 
 * Transmits a character over UART TX. Automatically converts '\n' to "\r\n"
 * for proper terminal display.
 * 
 * @param ch Character to transmit
 */
void uart_write(char ch);

/**
 * @brief Write data to UART using DMA (non-blocking)
 * 
 * Transmits a buffer over UART TX using DMA. Returns immediately if DMA
 * is busy with a previous transfer. Does NOT perform CRLF conversion.
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
 * Configures DMA1 Stream 5 / Channel 4 in circular mode to continuously
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
