#include "led2.h"

/* Bit mask for enabling GPIOA (bit 0) */
#define GPIOAEN       (1U<<0)
/* Bit mask for GPIOA pin 5 */
#define PIN5          (1U<<5)
/* Alias for PIN5 representing LED pin */
#define LED_PIN       PIN5

void led2_init(void)
{
    /* Enable clock access to GPIOA */
    RCC->AHB1ENR |= GPIOAEN;
    /* Set PA5 to output mode */
    GPIOA->MODER |= (1U<<10);  // Set bit 10 to 1
    GPIOA->MODER &= ~(1U<<11); // Set bit 11 to 0
}

void led2_on(void)
{
    GPIOA->ODR |= LED_PIN;
}

void led2_off(void)
{
    GPIOA->ODR &= ~LED_PIN;
}

void led2_toggle(void)
{
    GPIOA->ODR ^= LED_PIN;
}

int led2_get_state(void)
{
    return (GPIOA->IDR & LED_PIN) ? 1 : 0;
}
