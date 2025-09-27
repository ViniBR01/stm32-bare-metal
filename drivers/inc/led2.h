#ifndef LED2_H_
#define LED2_H_

#include "stm32f4xx.h"

void led2_init(void);
void led2_on(void);
void led2_off(void);
void led2_toggle(void);
int led2_get_state(void);

#endif /* LED2_H_ */
