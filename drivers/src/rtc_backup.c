#include "rtc_backup.h"

#include "stm32f4xx.h"

void rtc_backup_enable_writes(void)
{
    /* Clock the PWR controller so PWR->CR is reachable, then unlock the
     * backup domain.  Both bits are sticky across function calls so this
     * function is safe to invoke more than once. */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR; /* RM0383 §5.3.14 dummy read after enabling clock */
    PWR->CR      |= PWR_CR_DBP;
}

uint32_t rtc_backup_read_dr0(void)
{
    return RTC->BKP0R;
}

void rtc_backup_write_dr0(uint32_t value)
{
    RTC->BKP0R = value;
}
