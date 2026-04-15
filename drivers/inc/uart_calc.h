/*
 * uart_calc.h — Pure UART calculation functions (no register access).
 *
 * Exposed for host unit testing. These functions implement the mathematical
 * logic behind uart_init() baud-rate setup and the DMA circular-buffer
 * delivery logic. They take plain integers and return plain integers —
 * no peripheral struct access, no side effects.
 */

#ifndef UART_CALC_H_
#define UART_CALC_H_

#include <stdint.h>

/**
 * @brief Compute the USART BRR register value for a given baud rate.
 *
 * Uses rounded integer arithmetic:
 *   BRR = (periph_clk + baudrate / 2) / baudrate
 *
 * @param periph_clk  APB peripheral clock in Hz (e.g. 50 000 000)
 * @param baudrate    Desired baud rate in bps (e.g. 115200)
 * @return Value to write to USART->BRR
 */
uint16_t uart_compute_baud_divisor(uint32_t periph_clk, uint32_t baudrate);

/**
 * @brief Compute bytes received since the last DMA callback delivery.
 *
 * In DMA circular mode NDTR decrements from buf_size toward 0 as bytes
 * are received, then wraps back to buf_size.
 *
 *   head = buf_size - ndtr       (next write position in the buffer)
 *   tail = buf_size - last_ndtr  (position at last delivery)
 *
 * Returns the number of bytes available since the last delivery:
 *   No wrap (head >= tail): head - tail
 *   Wrap    (head <  tail): (buf_size - tail) + head
 *
 * @param ndtr       Current DMA NDTR value (remaining items)
 * @param last_ndtr  NDTR value at previous delivery
 * @param buf_size   Total circular buffer size in bytes
 * @return Number of newly received bytes
 */
uint16_t uart_circ_bytes_available(uint16_t ndtr, uint16_t last_ndtr,
                                   uint16_t buf_size);

#endif /* UART_CALC_H_ */
