#include "user_button.h"

/* Bit mask for enabling GPIOC (bit 2) */
#define GPIOCEN       (1U<<2)
/* Bit mask for GPIOC pin 13 */
#define PIN13         (1U<<13)
/* Alias for PIN13 representing B1 User */
#define B1_USER       PIN13

void user_button_init(void)
{
    /* Enable clock access to GPIOC */
    RCC->AHB1ENR |= GPIOCEN;
    /* Set PC13 to input mode */
    GPIOC->MODER &= ~(1U<<26); // Set bit 26 to 0
    GPIOC->MODER &= ~(1U<<27); // Set bit 27 to 0
}

int user_button_get_state(void)
{
    return (GPIOC->IDR & B1_USER) ? 1 : 0;
}
