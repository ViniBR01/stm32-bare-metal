#include <stdint.h>

#include "gpio_handler.h"
#include "stm32f4xx.h"
#include "uart_echo.h"

#define GPIOAEN                (1U<<0)
#define UART2EN                (1U<<17)

#define UART_TERMINAL_BAUDRATE   115200
#define SYS_CLOCK_FREQ         16000000
#define APB1_CLOCK_FREQ        SYS_CLOCK_FREQ
#define CR1_RE                 (1U<<2)
#define CR1_TE                 (1U<<3)
#define CR1_UE                 (1U<<13)
#define SR_TXE                 (1U<<7)

static uint16_t compute_uart_bd(uint32_t periph_clk, uint32_t baudrate) {
	return ((periph_clk +( baudrate/2U ))/baudrate);
}

static void uart_set_baudrate(uint32_t periph_clk, uint32_t baudrate){
	USART2->BRR  = compute_uart_bd(periph_clk,baudrate);
}

void uart_echo_init(void) {
	/* Set PA2 and PA3 modes to alternate function */
    gpio_clock_enable(GPIO_PORT_A);
	gpio_configure_pin(GPIO_PORT_A, 2, GPIO_MODE_AF);
    gpio_configure_pin(GPIO_PORT_A, 3, GPIO_MODE_AF);
	/* Set PA2 alternate function type to UART_TX (AF7) */
    GPIOA->AFR[0] &= ~(0xF<<8); // Clear alternate function
    GPIOA->AFR[0] |= (7U<<8);   // Set to AF7 (UART2_TX)
	/* Set PA3 alternate function type to UART_RX (AF7) */
	GPIOA->AFR[0] &= ~(0xF<<12); // Clear alternate function
	GPIOA->AFR[0] |= (7U<<12);   // Set to AF7 (UART2_RX)
    
    /* Enable UART2 clock */
    RCC->APB1ENR |= UART2EN;
    
    /* Configure baudrate */
    uart_set_baudrate(APB1_CLOCK_FREQ, UART_TERMINAL_BAUDRATE);
    
    /* Enable Tx/Rx and UART module */
    USART2->CR1 |= CR1_TE;
    USART2->CR1 |= CR1_RE;
	USART2->CR1 |= CR1_UE;
}

char uart_echo_read(void) {
	/* Wait until the RXNE (Read data register not empty) flag is set in the
	   status register */
	while (!(USART2->SR & (1U<<5)));
	/* Read the received character from the data register and return it */
	return (char)(USART2->DR & 0xFF);
}

void uart_echo_write(char ch) {
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
