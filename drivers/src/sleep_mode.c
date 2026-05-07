#include "sleep_mode.h"
#include "stm32f4xx.h"

void sleep_mode_init(void) {
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
}

void enter_sleep_mode(void) {
    SCB->SCR &= ~SCB_SCR_SLEEPONEXIT_Msk;
    __WFI();
}

void enter_stop_mode(void) {
    PWR->CR |= PWR_CR_CWUF;
    PWR->CR |= PWR_CR_LPDS | PWR_CR_FPDS;
    PWR->CR &= ~PWR_CR_PDDS;
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    __DSB();
    __WFI();
    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
}

void enter_standby_mode(uint8_t enable_wkup_pin) {
    PWR->CR |= PWR_CR_CWUF | PWR_CR_CSBF;
    PWR->CR |= PWR_CR_PDDS;
    if (enable_wkup_pin) {
        PWR->CSR |= PWR_CSR_EWUP;
    }
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    __DSB();
    __WFI();
}

int sleep_was_standby_wakeup(void) {
    return (PWR->CSR & PWR_CSR_SBF) ? 1 : 0;
}

void sleep_clear_standby_flag(void) {
    PWR->CR |= PWR_CR_CSBF;
}

void sleep_clear_wakeup_flag(void) {
    PWR->CR |= PWR_CR_CWUF;
}
