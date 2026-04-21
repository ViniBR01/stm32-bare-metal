#include "systick.h"
#include "irq_priorities.h"
#include "rcc.h"

/* Millisecond counter incremented by SysTick_Handler every 1 ms */
static volatile uint32_t s_tick_ms = 0;

#ifdef UNIT_TEST
/** Reset the tick counter to zero. For use in host unit test setUp() only. */
void systick_reset_for_test(void) { s_tick_ms = 0; }
#endif

void systick_init(void)
{
    uint32_t reload = (rcc_get_sysclk() / 1000U) - 1U;

    SysTick->LOAD = reload;
    SysTick->VAL  = 0U;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk   /* processor clock */
                  | SysTick_CTRL_TICKINT_Msk      /* enable interrupt */
                  | SysTick_CTRL_ENABLE_Msk;

    /* SysTick_IRQn == -1; NVIC_SetPriority handles negative IRQn via SCB->SHP */
    NVIC_SetPriority(SysTick_IRQn, IRQ_PRIO_TIMER);
}

void SysTick_Handler(void)
{
    s_tick_ms++;
}

uint32_t systick_get_ms(void)
{
    return s_tick_ms;
}

uint32_t systick_elapsed_since(uint32_t start_ms)
{
    /* Unsigned subtraction handles 32-bit wraparound correctly */
    return systick_get_ms() - start_ms;
}

void systick_delay_ms(uint32_t delay)
{
    uint32_t start = systick_get_ms();
    while (systick_elapsed_since(start) < delay) { /* spin */ }
}
