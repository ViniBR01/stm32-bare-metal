#include <stddef.h>

#include "cli.h"
#include "cli_commands.h"
#include "fault_handler.h"
#include "led2.h"
#include "printf.h"
#include "printf_dma.h"
#include "sleep_mode.h"
#include "uart.h"

#define MAX_CMD_SIZE 32

// Global CLI context and buffer
static cli_context_t g_cli;
static char g_cmd_buffer[MAX_CMD_SIZE];

// Command execution deferred to main loop
static volatile uint8_t command_pending = 0;

// Forward declarations
static void on_char_received(char ch);
static void process_pending_command(void);

// RX callback - called from USART2 interrupt when character received
static void on_char_received(char ch) {
    // Process character through CLI (echoes character immediately)
    cli_process_char(&g_cli, ch, uart_write);
    
    // If Enter was pressed, defer command execution to main loop
    if (ch == '\n' || ch == '\r') {
        command_pending = 1;
    }
}

// Process pending command in main loop (non-ISR context)
static void process_pending_command(void) {
    if (command_pending) {
        // Save command to history before execution resets anything
        cli_history_save(&g_cli);
        
        // Execute the command
        cli_execute_command(&g_cli);
        
        // Reset buffer position for next command
        g_cli.buffer_pos = 0;
        
        // Print prompt
        printf("\n> ");
        printf_dma_mark_pending();
        
        // Clear the pending flag
        command_pending = 0;
    }
}

int main(void) {
    // Initialize hardware
    led2_init();
    uart_init();
    sleep_mode_init();
    
    // Enable DIV_0 trapping and individual fault handlers
    fault_handler_init();
    
    // Initialize printf DMA buffering
    printf_dma_init();
    
    // Register UART callbacks
    uart_register_rx_callback(on_char_received);
    uart_register_tx_complete_callback(printf_dma_tx_complete_callback);
    
    // Initialize CLI with application commands
    size_t num_commands;
    const cli_command_t* commands = cli_commands_get_table(&num_commands);
    cli_init(&g_cli, commands, num_commands, g_cmd_buffer, MAX_CMD_SIZE);
    
    // Print welcome message
    cli_print_welcome("\n=== STM32 CLI Example (DMA + Interrupts) ===");
    printf("\n> ");
    printf_dma_mark_pending();

    // Main loop - process pending operations or sleep
    while (1) {
        // Process pending command execution (deferred from ISR)
        if (command_pending) {
            process_pending_command();
        }
        
        // Process pending DMA transmission (non-blocking)
        printf_dma_process();
        
        // Sleep until next interrupt if nothing pending
        if (!command_pending) {
            enter_sleep_mode();
        }
    }
}