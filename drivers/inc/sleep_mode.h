#ifndef SLEEP_MODE_H_
#define SLEEP_MODE_H_

#include <stdint.h>

void sleep_mode_init(void);
void enter_sleep_mode(void);

void enter_stop_mode(void);
void enter_standby_mode(uint8_t enable_wkup_pin);

int sleep_was_standby_wakeup(void);
void sleep_clear_standby_flag(void);
void sleep_clear_wakeup_flag(void);

#endif // SLEEP_MODE_H_
