#ifndef SYSTICK_H_
#define SYSTICK_H_

#include <stdint.h>

#include "stm32f4xx.h"

void systick_delay_ms(uint32_t delay);

#endif /* SYSTICK_H_ */
