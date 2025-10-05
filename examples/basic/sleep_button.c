#include <stdio.h>

#include "led2.h"
#include "pc13_exti.h"
#include "sleep_mode.h"
#include "uart_terminal.h"

uint8_t g_btn_press;

int main(void) {
    led2_init();
    pc13_exti_init();
    uart_terminal_init();
    sleep_mode_init();

    printf("Starting sleep mode example.\n");
    while (1) {
        if (g_btn_press) {
            g_btn_press = 0;
            printf("Button pressed!\n");
            led2_toggle();
        }
        printf("Entering sleep mode...\n");
        enter_sleep_mode(); // Enter sleep mode until an interrupt occurs
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
