#include <stddef.h>

#include "cli.h"
#include "led2.h"
#include "printf.h"
#include "sleep_mode.h"
#include "uart.h"

#define MAX_CMD_SIZE 32
#define PRINTF_BUFFER_SIZE 256

// Global CLI context and buffer
cli_context_t g_cli;
char g_cmd_buffer[MAX_CMD_SIZE];

// Double buffering for DMA transmission (non-blocking)
#define NUM_BUFFERS 2
static char printf_buffers[NUM_BUFFERS][PRINTF_BUFFER_SIZE];
static volatile uint16_t buffer_indices[NUM_BUFFERS] = {0, 0};
static volatile uint8_t active_buffer = 0;    // Buffer being filled
static volatile uint8_t tx_buffer = 0;         // Buffer being transmitted
static volatile uint8_t pending_tx = 0;        // Flag: transmission pending

// Command execution deferred to main loop
static volatile uint8_t command_pending = 0;

// Forward declarations
static int cmd_led_on(void);
static int cmd_led_off(void);
static int cmd_led_toggle(void);
static void on_char_received(char ch);
static void on_tx_complete(void);
static void try_swap_and_transmit(void);
static void process_pending_command(void);

// Command implementations
// NOTE: These are now called from main loop context (not ISR),
// so we can safely use printf() which accumulates to buffer
static int cmd_led_on(void) {
    led2_on();
    printf("LED2 turned on\n");
    return 0;  // Success
}

static int cmd_led_off(void) {
    led2_off();
    printf("LED2 turned off\n");
    return 0;  // Success
}

static int cmd_led_toggle(void) {
    led2_toggle();
    printf("LED2 toggled\n");
    return 0;  // Success
}

// Command table (help command is automatically added by CLI library)
static const cli_command_t commands[] = {
    {"led_on",     "Turn on LED2",      cmd_led_on},
    {"led_off",    "Turn off LED2",     cmd_led_off},
    {"led_toggle", "Toggle LED2 state", cmd_led_toggle},
};

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

// RX callback - called from USART2 interrupt when character received
static void on_char_received(char ch) {
    // Process character through CLI (echoes character immediately)
    cli_process_char(&g_cli, ch, uart_write);
    
    // If Enter was pressed, defer command execution to main loop
    if (ch == '\n' || ch == '\r') {
        command_pending = 1;
    }
}

// TX complete callback - called from DMA interrupt when transfer done
static void on_tx_complete(void) {
    // Reset the buffer index of the buffer that just finished transmitting
    buffer_indices[tx_buffer] = 0;
}

// Process pending command in main loop (non-ISR context)
static void process_pending_command(void) {
    if (command_pending) {
        // Execute the command
        cli_execute_command(&g_cli);
        
        // Reset buffer position for next command
        g_cli.buffer_pos = 0;
        
        // Print prompt
        printf("> ");
        pending_tx = 1;  // Mark transmission as pending
        
        // Clear the pending flag
        command_pending = 0;
    }
}

int main(void) {
    // Initialize hardware
    led2_init();
    uart_init();
    sleep_mode_init();
    
    // Register UART callbacks
    uart_register_rx_callback(on_char_received);
    uart_register_tx_complete_callback(on_tx_complete);
    
    // Initialize CLI
    cli_init(&g_cli, commands, sizeof(commands)/sizeof(commands[0]), 
             g_cmd_buffer, MAX_CMD_SIZE);
    
    // Print welcome message
    cli_print_welcome("\n=== STM32 CLI Example (DMA + Interrupts) ===");
    printf("\n> ");
    pending_tx = 1;  // Mark transmission as pending

    // Main loop - process pending operations or sleep
    while (1) {
        // Process pending command execution (deferred from ISR)
        if (command_pending) {
            process_pending_command();
        }
        
        // Process pending DMA transmission (non-blocking)
        if (pending_tx && !uart_is_tx_busy()) {
            try_swap_and_transmit();
        }
        
        // Sleep until next interrupt if nothing pending
        if (!command_pending && !pending_tx) {
            enter_sleep_mode();
        }
    }
}