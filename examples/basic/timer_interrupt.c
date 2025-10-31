#include "led2.h"
#include "stm32f4xx.h"
// #include "tim2_uie.h"

volatile uint32_t g_toggle_led;

#define SR_UIF (1U<<0)
#define TIM2EN (1U<<0)
#define CR1_CEN (1U<<0)
#define DIER_UIE (1U<<0)

void tim2_1hz_init(void) {
    // Enable TIM2 clock
    RCC->APB1ENR |= TIM2EN;

    // Set prescaler and auto-reload for 1 Hz
    TIM2->PSC = 1600 - 1;
    TIM2->ARR = 5000 - 1;
    TIM2->CNT = 0;

    // Enable update interrupt
    TIM2->DIER |= DIER_UIE;
    NVIC_EnableIRQ(TIM2_IRQn);

    // Start the timer
    TIM2->CR1 = CR1_CEN;
}

void TIM2_IRQHandler(void) {
    if (TIM2->SR & SR_UIF) {  // Check if pending bit is set
        TIM2->SR &= ~SR_UIF;  // Clear pending bit
        g_toggle_led = 1;
    }
}

int main() {
    led2_init();
    g_toggle_led = 1;
    tim2_1hz_init();

    while(1) {
        if (g_toggle_led) {
            led2_toggle();
            g_toggle_led = 0;
        }
    }

    return 0;
}
