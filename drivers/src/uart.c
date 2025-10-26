#include <stdint.h>

#include "gpio_handler.h"
#include "stm32f4xx.h"
#include "uart.h"

/* Register bit definitions */
#define UART2EN                (1U<<17)
#define CR1_RE                 (1U<<2)
#define CR1_TE                 (1U<<3)
#define CR1_UE                 (1U<<13)
#define SR_TXE                 (1U<<7)
#define SR_RXNE                (1U<<5)

/* Configuration constants */
#define UART_BAUDRATE          115200
#define SYS_CLOCK_FREQ         16000000
#define APB1_CLOCK_FREQ        SYS_CLOCK_FREQ

/* Private function prototypes */
static uint16_t compute_uart_bd(uint32_t periph_clk, uint32_t baudrate);
static void uart_set_baudrate(uint32_t periph_clk, uint32_t baudrate);

void uart_init(void) {
    /* Enable GPIOA clock and configure PA2 (TX) and PA3 (RX) */
    gpio_clock_enable(GPIO_PORT_A);
    gpio_configure_pin(GPIO_PORT_A, 2, GPIO_MODE_AF);
    gpio_configure_pin(GPIO_PORT_A, 3, GPIO_MODE_AF);
    
    /* Set PA2 alternate function to UART_TX (AF7) */
    GPIOA->AFR[0] &= ~(0xF<<8);  // Clear AF for PA2
    GPIOA->AFR[0] |= (7U<<8);    // Set AF7 (USART2_TX)
    
    /* Set PA3 alternate function to UART_RX (AF7) */
    GPIOA->AFR[0] &= ~(0xF<<12); // Clear AF for PA3
    GPIOA->AFR[0] |= (7U<<12);   // Set AF7 (USART2_RX)
    
    /* Enable UART2 clock */
    RCC->APB1ENR |= UART2EN;
    
    /* Configure baudrate */
    uart_set_baudrate(APB1_CLOCK_FREQ, UART_BAUDRATE);
    
    /* Enable transmitter, receiver, and UART module */
    USART2->CR1 |= CR1_TE;
    USART2->CR1 |= CR1_RE;
    USART2->CR1 |= CR1_UE;
}

char uart_read(void) {
    /* Wait until RXNE (Read data register not empty) flag is set */
    while (!(USART2->SR & SR_RXNE));
    
    /* Read and return the received character */
    return (char)(USART2->DR & 0xFF);
}

void uart_write(char ch) {
    /* If sending a newline, transmit carriage return first for proper
       terminal display (LF -> CRLF conversion) */
    if (ch == '\n') {
        while (!(USART2->SR & SR_TXE));
        USART2->DR = ('\r' & 0xFF);
    }
    
    /* Wait until transmit data register is empty */
    while (!(USART2->SR & SR_TXE));
    
    /* Write character to transmit data register */
    USART2->DR = (ch & 0xFF);
}

/* Private function implementations */

static uint16_t compute_uart_bd(uint32_t periph_clk, uint32_t baudrate) {
    return ((periph_clk + (baudrate / 2U)) / baudrate);
}

static void uart_set_baudrate(uint32_t periph_clk, uint32_t baudrate) {
    USART2->BRR = compute_uart_bd(periph_clk, baudrate);
}

