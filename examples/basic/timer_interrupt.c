#include "led2.h"
#include "rcc.h"
#include "stm32f4xx.h"

volatile uint32_t g_toggle_led;

#define SR_UIF (1U<<0)
#define TIM2EN (1U<<0)
#define CR1_CEN (1U<<0)
#define DIER_UIE (1U<<0)

#define TIM2_TICK_HZ   10000U
#define TIM2_PERIOD_HZ     2U

void tim2_1hz_init(void) {
    RCC->APB1ENR |= TIM2EN;

    uint32_t timer_clk = rcc_get_apb1_timer_clk();
    TIM2->PSC = (timer_clk / TIM2_TICK_HZ) - 1;
    TIM2->ARR = (TIM2_TICK_HZ / TIM2_PERIOD_HZ) - 1;
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
