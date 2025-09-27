#ifndef USER_BUTTON_H_
#define USER_BUTTON_H_

#include "stm32f4xx.h"

void user_button_init(void);
int user_button_get_state(void);

#endif /* USER_BUTTON_H_ */
