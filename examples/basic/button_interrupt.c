#include <stdio.h>

#include "exti_handler.h"
#include "led2.h"
#include "uart_terminal.h"

#define BUTTON_PIN (13)

volatile uint8_t g_btn_press;

int main(void) {
    led2_init();
    uart_terminal_init();
    int res = exti_configure_gpio_interrupt(GPIO_PORT_C, BUTTON_PIN, 
        EXTI_TRIGGER_FALLING, EXTI_MODE_INTERRUPT);
    if (res) {
        printf("Error to configure gpio interrupt.\r\n");
        // Do something to recover!
    }

    printf("Starting button press example.\n");
    
    while(1) {
        if (g_btn_press) {
            g_btn_press = 0;
            printf("Button pressed!\n");
            led2_toggle();
        }
    }
}

static void exti_callback(void) {
    g_btn_press = 1;
}

void EXTI15_10_IRQHandler(void) {
    if (exti_is_pending(BUTTON_PIN)) { // Check if pending bit is set
        exti_clear_pending(BUTTON_PIN);  // Clear pending bit
        exti_callback();
    }
}
