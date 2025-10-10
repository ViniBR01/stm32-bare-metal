#include <stdio.h>

#include "exti_handler.h"
#include "led2.h"
#include "uart_terminal.h"

volatile uint8_t g_btn_press;

int main(void) {
    led2_init();
    uart_terminal_init();
    int res = exti_configure_gpio_interrupt(GPIO_PORT_C, 13, 
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
    if (EXTI->PR & (1U<<13)) { // Check if pending bit for line 13 is set
        EXTI->PR |= (1U<<13);  // Clear pending bit by writing 1
        exti_callback();        // Call the callback function
    }
}
