#include "led2.h"
#include "log_c.h"
#include "systick.h"
#include "uart.h"

// Custom _putchar implementation (required by printf library)
// This overrides the weak symbol in log_c
void _putchar(char character) {
    uart_write(character);
}

int main(void) {
    uart_init();
    led2_init();
    
    loginfo("Hello, UART Terminal!");
    loginfo("UART initialized successfully!");
    
    loginfo("Starting LED blink test...");

    uint32_t count = 0;
    while (1) {
        led2_off();
        systick_delay_ms(200);
        led2_on();
        systick_delay_ms(800);
        
        if (++count < 10) {
            loginfo("Tick... count=%lu", count);
        } else if (count % 10 == 0) {
            loginfo("Tick... count=%lu", count);
        }
    }
}
