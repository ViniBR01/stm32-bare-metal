#include "led2.h"
#include "stm32f4xx.h"
// #include "timer_handler.h"

// volatile uint32_t g_toggle_led;

#define SR_UIF (1U<<0)
#define TIM2EN (1U<<0)
#define CR1_CEN (1U<<0)

void tim2_1hz_init(void) {
    RCC->APB1ENR |= TIM2EN;

    TIM2->PSC = 1600 - 1;
    TIM2->ARR = 10000 - 1;
    TIM2->CNT = 0;

    TIM2->CR1 = CR1_CEN;
}

int main() {
    led2_init();
    //    timer_init();
    //    g_toggle_led = 0;
    tim2_1hz_init();

    while(1) {
        //        if (g_toggle_led) {
        led2_toggle();
        //            g_toggle_led = 0;
        //        }
        while (!(TIM2->SR & SR_UIF)) {}
        TIM2->SR &= ~SR_UIF;
    }

    return 0;
}
