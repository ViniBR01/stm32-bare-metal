#include "stm32f4xx.h"
/* Bit mask for enabling GPIOA (bit 0) */
#define GPIOAEN       (1U<<0)
/* Bit mask for enabling GPIOC (bit 2) */
#define GPIOCEN       (1U<<2)
/* Bit mask for GPIOA pin 5 */
#define PIN5          (1U<<5)
/* Bit mask for GPIOC pin 13 */
#define PIN13         (1U<<13)
/* Alias for PIN5 representing LED pin */
#define LED_PIN       PIN5
/* Alian for PIN13 representing B1 User */
#define B1_USER       PIN13

int main(void)
{
    /* Enable clock access to GPIOA and GPIOC */
    RCC->AHB1ENR |= GPIOAEN;
    RCC->AHB1ENR |= GPIOCEN;
    /* Set PA5 to output mode */
    GPIOA->MODER |= (1U<<10);  // Set bit 10 to 1
    GPIOA->MODER &= ~(1U<<11); // Set bit 11 to 0
    /* Set PC13 to input mode */
    GPIOC->MODER &= ~(1U<<26); // Set bit 26 to 0
    GPIOC->MODER &= ~(1U<<27); // Set bit 27 to 0

    while(1)
    {
        GPIOA->ODR = GPIOC->IDR & B1_USER ? 0 : LED_PIN;
    }
}
