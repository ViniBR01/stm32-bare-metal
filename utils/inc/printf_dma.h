#ifndef PRINTF_DMA_H
#define PRINTF_DMA_H

#include <stdint.h>

/**
 * @brief Initialize the printf DMA buffering system
 * 
 * Sets up double buffering for non-blocking printf output via DMA.
 * Must be called before any printf() calls that use DMA transmission.
 */
void printf_dma_init(void);

/**
 * @brief Check if there's a pending transmission and start it if UART is idle
 * 
 * Call this periodically from main loop to process buffered output.
 * This function is non-blocking and will only transmit if the UART is not busy.
 */
void printf_dma_process(void);

/**
 * @brief TX complete callback - call from DMA interrupt
 * 
 * This function should be called from the UART TX complete interrupt
 * to reset buffer state after transmission completes.
 */
void printf_dma_tx_complete_callback(void);

/**
 * @brief Mark that data is pending transmission
 * 
 * Call this after printf() calls to trigger transmission in the main loop.
 */
void printf_dma_mark_pending(void);

/**
 * @brief Flush all buffered printf data synchronously (blocking)
 *
 * Triggers a DMA transmission of any buffered printf data and busy-waits
 * until the transfer completes. Use this when a function produces more
 * output than a single printf buffer can hold without returning to the
 * main loop.
 *
 * Must only be called from main-loop context (not from ISR).
 */
void printf_dma_flush(void);

#endif /* PRINTF_DMA_H */

