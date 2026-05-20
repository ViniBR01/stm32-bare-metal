#include "timer.h"
#include "gpio_handler.h"
#include "systick.h"
#include "stm32f4xx.h"

#define PWM_FREQ_HZ    200U
#define PWM_STEPS      100U

int main(void)
{
    /* Configure PA5 as TIM2_CH1 alternate function (AF1) */
    gpio_clock_enable(GPIO_PORT_A);
    gpio_configure_pin(GPIO_PORT_A, 5, GPIO_MODE_AF);
    GPIOA->AFR[0] |= (1 << (5 * 4));   /* AF1 for TIM2_CH1 */

    timer_pwm_init(TIMER_2, TIMER_CH1, PWM_FREQ_HZ, PWM_STEPS);
    timer_start(TIMER_2);

    int duty_cycle = 0;
    int direction = 1;
    while (1)
    {
        timer_pwm_set_duty(TIMER_2, TIMER_CH1, (uint32_t)duty_cycle);
        systick_delay_ms(10);

        duty_cycle += direction;
        if (duty_cycle >= 100 || duty_cycle <= 0) {
            direction = -direction;
        }
    }
}
