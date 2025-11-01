#include "stm32f4xx.h"
#include "gpio_handler.h"
#include "systick.h"

#define SR_UIF (1U<<0)
#define TIM2EN (1U<<0)
#define CR1_CEN (1U<<0)

void tim2_ch1_pwm_init(void) {
    // First, initialize GPIOA Pin 5 for TIM2 Channel 1 PWM output
    gpio_clock_enable(GPIO_PORT_A);
    gpio_configure_pin(GPIO_PORT_A, 5, GPIO_MODE_AF); // PA5 as AF (TIM2_CH1)
    // Set AF for PA5 to TIM2_CH1 (AF1)
    GPIOA->AFR[0] |= (1 << (5 * 4));

    // Enable TIM2 clock
    RCC->APB1ENR |= TIM2EN;

    // Set prescaler and auto-reload for 1 kHz PWM frequency
    TIM2->PSC = 800 - 1;      // Prescaler
    TIM2->ARR = 100 - 1;    // Auto-reload for 1 kHz

    // Configure PWM mode 1 on Channel 1
    TIM2->CCMR1 |= (6 << 4); // OC1M: PWM mode 1
    TIM2->CCMR1 |= (1 << 3); // Enable preload for CCR1
    TIM2->CCER |= 1;         // Enable output on Channel 1
    TIM2->CCR1 = 0;          // Initial duty cycle 0%

    // Start the timer
    TIM2->CR1 = CR1_CEN;
}

void tim2_ch1_pwm_set_duty_cycle(int duty_cycle)
{
    if (duty_cycle < 0) duty_cycle = 0;
    if (duty_cycle > 100) duty_cycle = 100;
    TIM2->CCR1 = duty_cycle;
}

int main(void)
{
    tim2_ch1_pwm_init();

    int duty_cycle = 0;
    int direction = 1; // 1 for increasing, -1 for decreasing
    while(1)
    {
        tim2_ch1_pwm_set_duty_cycle(duty_cycle); // 50% duty cycle
	    systick_delay_ms(10);

        duty_cycle += direction;
        if (duty_cycle >= 100 || duty_cycle <= 0) {
            direction = -direction; // Reverse direction
        }
    }
}
