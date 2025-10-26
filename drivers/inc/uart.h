#ifndef UART_H_
#define UART_H_

/**
 * @brief Initialize UART2 for communication
 * 
 * Configures USART2 on PA2 (TX) and PA3 (RX) with 115200 baud rate.
 * Enables both transmit and receive functionality.
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

#endif /* UART_H_ */

