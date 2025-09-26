#include "stm32f4xx.h"
/* Bit mask for enabling GPIOA (bit 0) */
#define GPIOAEN       (1U<<0)
/* Bit mask for GPIOA pin 5 */
#define PIN5          (1U<<5)
/* Alias for PIN5 representing LED pin */
#define LED_PIN       PIN5

int main(void)
{
    /* Enable clock access to GPIOA */
    RCC->AHB1ENR |= GPIOAEN;
    /* Set PA5 to output mode */
    GPIOA->MODER |= (1U<<10);  // Set bit 10 to 1
    GPIOA->MODER &= ~(1U<<11); // Set bit 11 to 0

    while(1)
    {
        GPIOA->ODR ^= LED_PIN;
        for(int i = 0; i < 1000000; i++) {}
    }
}
