#include "led2.h"
#include "exti_handler.h"
#include "sleep_mode.h"
#include "uart.h"
#include "printf.h"

uint8_t g_btn_press;

void _putchar(char character) {
    uart_write(character);
}

int main(void) {
    led2_init();
    exti_configure_gpio_interrupt(GPIO_PORT_C, 13, EXTI_TRIGGER_FALLING, EXTI_MODE_INTERRUPT);
    uart_init();
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
    if (exti_is_pending(13)) { // Check if pending bit for line 13 is set
        exti_clear_pending(13);  // Clear pending bit
        exti_callback();        // Call the callback function
    }
}
