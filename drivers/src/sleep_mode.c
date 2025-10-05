/*
 * sleep_mode.c
 *
 * Created on: 24 de jun de 2024
 * 
 * By default, the microcontroller is in Run mode after a system or a power-on reset.
 * In Run mode the CPU is clocked by HCLK and the program code is executed.
 * The microcontroller can enter Sleep mode by executing the WFI (Wait For Interrupt)
 * instruction. In Sleep mode the CPU clock is stopped while the HCLK, PCLK1,
 * PCLK2, and the peripheral clocks are running.
 * The microcontroller returns to Run mode when an interrupt or a reset occurs.
 * In Sleep mode, the consumption is reduced down to 15 µA (typical value at 3 V
 * and 25 °C) in Sleep mode with the regulator in main mode.
 * 
 */
#include "sleep_mode.h"
#include "stm32f4xx.h"

/* Initialize sleep mode settings
 *
 * The SLEEPDEEP bit in the Cortex System Control Register (SCR) must be set
 */
void sleep_mode_init(void) {
    // Enable Power Control clock
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    // Clear SLEEPDEEP bit of Cortex System Control Register
    SCB->SCR &= ~(SCB_SCR_SLEEPDEEP_Msk);
}

/* Low-power modes are entered by the MCU by executing the WFI (Wait For Interrupt),
 * or WFE (Wait for Event) instructions, or when the SLEEPONEXIT bit in the Cortex-M4
 * System Control register is set on Return from ISR.
 * Entering Low-power mode through WFI or WFE is executed only if no interrupt is
 * pending or no event is pending.
 * In this example, we use WFI to enter sleep mode.
 */
void enter_sleep_mode(void) {
    // Clear SLEEPONEXIT bit of Cortex System Control Register
    SCB->SCR &= ~SCB_SCR_SLEEPONEXIT_Msk;
    // Enter Sleep mode, wake up on any interrupt
    __WFI();
}
