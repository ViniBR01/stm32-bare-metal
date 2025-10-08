#include <stdarg.h>
#include <stddef.h>

#include "uart_dma.h"

extern volatile uint8_t g_rx_cmplt;
extern volatile uint8_t g_uart_cmplt;
extern volatile uint8_t g_tx_cmplt;

extern char uart_data_buffer[UART_DATA_BUFF_SIZE];
char msg_buff[150] = {'\0'};

// Custom strlen implementation for -nostd builds
static size_t my_strlen(const char *s) {
    size_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

// Custom snprintf implementation for -nostd builds
int my_snprintf(char *str, size_t size, const char *format, ...) {
    va_list args;
    va_start(args, format);

    size_t written = 0;
    while (*format != '\0' && written < size - 1) {
        if (*format == '%') {
            format++;
            if (*format == 's') {
                const char *s = va_arg(args, const char *);
                size_t len = my_strlen(s);
                for (size_t i = 0; i < len && written < size - 1; i++) {
                    str[written++] = s[i];
                }
            }
            // Add other format specifiers here if needed
        } else {
            str[written++] = *format;
        }
        format++;
    }

    str[written] = '\0';
    va_end(args);
    return written;
}


int main(void) {
    uart2_rxtx_init();
    dma1_init();
    dma1_stream5_uart_rx_config();
    my_snprintf(msg_buff, sizeof(msg_buff), "Initialization complete\n\r");
    dma1_stream6_uart_tx_config((uint32_t)msg_buff, my_strlen(msg_buff));

    while (!g_tx_cmplt) {
    }

    while (1) {
        if (g_rx_cmplt) {
            g_rx_cmplt = 0;
            my_snprintf(msg_buff, sizeof(msg_buff), "Message received : %s \r\n", uart_data_buffer);
            g_tx_cmplt = 0;
            g_uart_cmplt = 0;
            dma1_stream6_uart_tx_config((uint32_t)msg_buff, my_strlen(msg_buff));
            while (!g_tx_cmplt) {
            }
            clear_uart_data_buffer();
            dma1_stream5_uart_rx_config();
        }
    }
}
