#include "printf_dma.h"
#include "uart.h"
#include <stddef.h>

#define PRINTF_BUFFER_SIZE 256
#define NUM_BUFFERS 2

// Double buffering for DMA transmission (non-blocking)
static char printf_buffers[NUM_BUFFERS][PRINTF_BUFFER_SIZE];
static volatile uint16_t buffer_indices[NUM_BUFFERS] = {0, 0};
static volatile uint8_t active_buffer = 0;    // Buffer being filled
static volatile uint8_t tx_buffer = 0;         // Buffer being transmitted
static volatile uint8_t pending_tx = 0;        // Flag: transmission pending

// Forward declaration
static void try_swap_and_transmit(void);

void printf_dma_init(void) {
    // Buffers are already zero-initialized (static)
    active_buffer = 0;
    tx_buffer = 0;
    pending_tx = 0;
    buffer_indices[0] = 0;
    buffer_indices[1] = 0;
}

void printf_dma_mark_pending(void) {
    pending_tx = 1;
}

void printf_dma_process(void) {
    if (pending_tx && !uart_is_tx_busy()) {
        try_swap_and_transmit();
    }
}

void printf_dma_tx_complete_callback(void) {
    // Reset the buffer index of the buffer that just finished transmitting
    buffer_indices[tx_buffer] = 0;
}

// Callback for _putchar used by printf library
// This is called from both ISR and main loop context
void _putchar(char character) {
    uint8_t buf_idx = active_buffer;
    
    // Convert LF to CRLF for proper terminal display
    // (uart_write_dma does not perform this conversion automatically)
    if (character == '\n') {
        // Add CR before LF, ensuring space for both characters
        if (buffer_indices[buf_idx] < PRINTF_BUFFER_SIZE - 2) {
            printf_buffers[buf_idx][buffer_indices[buf_idx]++] = '\r';
            printf_buffers[buf_idx][buffer_indices[buf_idx]++] = '\n';
        }
        // Mark transmission as pending (will be handled in main loop)
        pending_tx = 1;
    } else {
        // Add character to active buffer
        if (buffer_indices[buf_idx] < PRINTF_BUFFER_SIZE - 1) {
            printf_buffers[buf_idx][buffer_indices[buf_idx]++] = character;
        }
        
        // Mark transmission as pending if buffer is nearly full
        if (buffer_indices[buf_idx] >= PRINTF_BUFFER_SIZE - 1) {
            pending_tx = 1;
        }
    }
}

void printf_dma_flush(void) {
    printf_dma_mark_pending();
    printf_dma_process();
    while (uart_is_tx_busy());
}

// Swap buffers and transmit via DMA (non-blocking)
// Called from main loop when pending_tx is set and DMA is idle
static void try_swap_and_transmit(void) {
    if (pending_tx && !uart_is_tx_busy()) {
        // Get the buffer to transmit (current active buffer with data)
        uint8_t buf_to_send = active_buffer;
        uint16_t len_to_send = buffer_indices[buf_to_send];
        
        if (len_to_send > 0) {
            // Swap to the other buffer for new accumulation
            active_buffer = (active_buffer + 1) % NUM_BUFFERS;
            tx_buffer = buf_to_send;
            
            // Start DMA transmission on the buffer with accumulated data
            uart_write_dma(printf_buffers[tx_buffer], len_to_send);
            
            // Clear pending flag
            pending_tx = 0;
        } else {
            // Nothing to send, just clear the flag
            pending_tx = 0;
        }
    }
}

