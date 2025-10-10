#ifndef LED2_H_
#define LED2_H_

#include <stdint.h>

void led2_init(void);
void led2_on(void);
void led2_off(void);
void led2_toggle(void);
uint8_t led2_get_state(void);

#endif /* LED2_H_ */
