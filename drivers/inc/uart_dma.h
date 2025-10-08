#ifndef __UART_DMA_H__
#define __UART_DMA_H__
#include <stdint.h>
#include "stm32f4xx.h"

#define UART_DATA_BUFF_SIZE		6

extern volatile uint8_t g_rx_cmplt;
extern volatile uint8_t g_tx_cmplt;
extern volatile uint8_t g_uart_cmplt;
extern char uart_data_buffer[UART_DATA_BUFF_SIZE];

void uart2_rxtx_init(void);
void dma1_init(void);
void dma1_stream5_uart_rx_config(void);
void dma1_stream6_uart_tx_config(uint32_t msg_to_snd, uint32_t msg_len);
void clear_uart_data_buffer(void);

#endif /* __UART_DMA_H__ */
