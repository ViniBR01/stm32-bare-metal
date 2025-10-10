#include "led2.h"
#include "gpio_handler.h"

#define LED2PORT   (GPIO_PORT_A)
#define LED2PIN    (5U)
#define LED2MODE   (GPIO_MODE_OUTPUT)

void led2_init(void)
{
    gpio_clock_enable(LED2PORT);
    gpio_configure_pin(LED2PORT, LED2PIN, LED2MODE);
}

void led2_on(void)
{
    gpio_set_pin(LED2PORT, LED2PIN);
}

void led2_off(void)
{
    gpio_clear_pin(LED2PORT, LED2PIN);
}

void led2_toggle(void)
{
    gpio_toggle_pin(LED2PORT, LED2PIN);
}

uint8_t led2_get_state(void)
{
    return gpio_read_pin(LED2PORT, LED2PIN);
}
