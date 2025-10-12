#include <stdint.h>

#include "stm32f4xx.h"
#include "uart_terminal.h"

#define GPIOAEN                (1U<<0)
#define UART2EN                (1U<<17)

#define UART_TERMINAL_BAUDRATE   115200
#define SYS_CLOCK_FREQ         16000000
#define APB1_CLOCK_FREQ        SYS_CLOCK_FREQ
#define CR1_TE                 (1U<<3)
#define CR1_UE                 (1U<<13)
#define SR_TXE                 (1U<<7)

static void uart_set_baudrate(uint32_t peripheral_clock, uint32_t baudrate);

void uart_terminal_init(void) {
    /* Enable GPIOA clock */
    RCC->AHB1ENR |= GPIOAEN;
    /* Set PA2 mode to alternate function */
    GPIOA->MODER &= ~(3U<<4); // Clear mode
    GPIOA->MODER |= (2U<<4);  // Set to AF mode
    /* Set PA2 alternate function type to UART_TX (AF7) */
    GPIOA->AFR[0] &= ~(0xF<<8); // Clear alternate function
    GPIOA->AFR[0] |= (7U<<8);   // Set to AF7 (UART2_TX)
    
    /* Enable UART2 clock */
    RCC->APB1ENR |= UART2EN;
    
    /* Configure baudrate */
    uart_set_baudrate(APB1_CLOCK_FREQ, UART_TERMINAL_BAUDRATE);
    
    /* Enable transmitter and UART module */
    USART2->CR1 |= CR1_TE;
    USART2->CR1 |= CR1_UE;
}

static uint16_t compute_uart_bd(uint32_t peripheral_clock, uint32_t baudrate) {
	return ((peripheral_clock +( baudrate/2U ))/baudrate);
}

static void uart_set_baudrate(uint32_t peripheral_clock, uint32_t baudrate) {
	USART2->BRR  = compute_uart_bd(peripheral_clock, baudrate);
}

/* Make uart_write visible to other functions in this compilation unit
    (and to the _putchar wrapper below). */
static void uart_write(int ch) {
    /* If sending a newline, transmit a carriage return first so terminals
       that don't auto-map LF->CRLF will move the cursor to column 0. */
    if (ch == '\n') {
        while (!(USART2->SR & SR_TXE));
        USART2->DR = ('\r' & 0xFF);
    }
    /* Wait until transmit data register is empty */
    while (!(USART2->SR & SR_TXE));
    /* Write character to transmit data register */
    USART2->DR = (ch & 0xFF);
}

/* Adapter required by the 3rd-party printf implementation. The
   printf library expects a function named _putchar(char). We forward
   to uart_write which already handles CR/LF behavior and TXE polling. */
void _putchar(char character) {
    uart_write((int)character);
}
