#include "systick.h"

#define CTRL_ENABLE             (1U<<0)
#define CTRL_CLKSRC             (1u<<2)
#define CTRL_COUNTFLAG          (1u<<16)

/* Assume a base clock of 16 MHz */
#define TICKS_IN_1MS            16000

void systick_delay_ms(uint32_t delay)
{
  SysTick->LOAD = TICKS_IN_1MS - 1;
  SysTick->VAL = 0; // Resets current value
  SysTick->CTRL |= CTRL_CLKSRC;

  SysTick->CTRL |= CTRL_ENABLE;

  for(int i = 0; i < delay; ++i)
    {
      while((SysTick->CTRL & CTRL_COUNTFLAG) == 0) {}
    }

  SysTick->CTRL = 0; // Disables Systick
}
