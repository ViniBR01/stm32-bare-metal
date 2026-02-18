#include "systick.h"
#include "rcc.h"

#define CTRL_ENABLE             (1U<<0)
#define CTRL_CLKSRC             (1u<<2)
#define CTRL_COUNTFLAG          (1u<<16)

void systick_delay_ms(uint32_t delay)
{
  SysTick->LOAD = (rcc_get_sysclk() / 1000) - 1;
  SysTick->VAL = 0;
  SysTick->CTRL |= CTRL_CLKSRC;

  SysTick->CTRL |= CTRL_ENABLE;

  for(int i = 0; i < delay; ++i)
    {
      while((SysTick->CTRL & CTRL_COUNTFLAG) == 0) {}
    }

  SysTick->CTRL = 0;
}
