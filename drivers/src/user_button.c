#include "user_button.h"
#include "gpio_handler.h"

#define BUTTON_PORT (GPIO_PORT_C)
#define BUTTON_PIN  (13)
#define BUTTON_MODE (GPIO_MODE_INPUT)

void user_button_init(void)
{
    gpio_clock_enable(BUTTON_PORT);
    gpio_configure_pin(BUTTON_PORT, BUTTON_PIN, BUTTON_MODE);
}

uint8_t user_button_get_state(void)
{
    return gpio_read_pin(BUTTON_PORT, BUTTON_PIN);
}
