#include <stdio.h>

#include "led2.h"
#include "uart_terminal.h"
#include "pc13_exti.h"

uint8_t g_btn_press;

int main(void) {
    led2_init();
    uart_terminal_init();
    pc13_exti_init();

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
