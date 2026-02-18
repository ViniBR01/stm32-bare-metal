#include "stm32f4xx.h"
#include "gpio_handler.h"
#include "rcc.h"
#include "systick.h"

#define SR_UIF (1U<<0)
#define TIM2EN (1U<<0)
#define CR1_CEN (1U<<0)

#define PWM_FREQ_HZ    200U
#define PWM_STEPS      100U

void tim2_ch1_pwm_init(void) {
    gpio_clock_enable(GPIO_PORT_A);
    gpio_configure_pin(GPIO_PORT_A, 5, GPIO_MODE_AF);
    GPIOA->AFR[0] |= (1 << (5 * 4));

    RCC->APB1ENR |= TIM2EN;

    uint32_t timer_clk = rcc_get_apb1_timer_clk();
    TIM2->PSC = (timer_clk / (PWM_FREQ_HZ * PWM_STEPS)) - 1;
    TIM2->ARR = PWM_STEPS - 1;

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
