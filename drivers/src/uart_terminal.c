#include <stdint.h>

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
static void uart_write(int ch);

int __io_putchar(int ch) {
    uart_write(ch);
    return ch;
}

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

static void uart_set_baudrate(uint32_t peripheral_clock, uint32_t baudrate) {
    USART2->BRR = peripheral_clock / baudrate;
}

static void uart_write(int ch) {
    /* Wait until transmit data register is empty */
    while (!(USART2->SR & SR_TXE));
    /* Write character to transmit data register */
    USART2->DR = (ch & 0xFF);
}
