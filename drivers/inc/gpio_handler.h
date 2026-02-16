#ifndef GPIO_HANDLER_H_
#define GPIO_HANDLER_H_

#include <stdint.h>

/**
 * @brief Enumeration for GPIO ports
 */
typedef enum {
    GPIO_PORT_A,
    GPIO_PORT_B,
    GPIO_PORT_C,
    GPIO_PORT_D,
    GPIO_PORT_E,
    GPIO_PORT_H
} gpio_port_t;

/**
 * @brief GPIO Pin Modes
 */
typedef enum {
    GPIO_MODE_INPUT = 0,
    GPIO_MODE_OUTPUT,
    GPIO_MODE_AF,
    GPIO_MODE_ANALOG,
    GPIO_MODE_INVALID
} gpio_mode_t;

/**
 * @brief Enables the clock for a specific GPIO port.
 * @param port The GPIO port (GPIO_PORT_A, GPIO_PORT_B, etc.).
 */
void gpio_clock_enable(gpio_port_t port);

/**
 * @brief Disables the clock for a specific GPIO port.
 * @param port The GPIO port (GPIO_PORT_A, GPIO_PORT_B, etc.).
 */
void gpio_clock_disable(gpio_port_t port);

/**
 * @brief Configures the mode of a specific GPIO pin.
 * @param port The GPIO port(GPIO_PORT_A, GPIO_PORT_B, etc.). 
 * @param pin_num The pin number (0-15).
 * @param mode The mode to set (INPUT, OUTPUT, AF, ANALOG).
 */
void gpio_configure_pin(gpio_port_t port, uint8_t pin_num, gpio_mode_t mode);

/**
 * @brief Sets a GPIO pin to a high state.
 * @param port The GPIO port (GPIO_PORT_A, GPIO_PORT_B, etc.).
 * @param pin_num The pin number (0-15).
 */
void gpio_set_pin(gpio_port_t port, uint8_t pin_num);

/**
 * @brief Clears a GPIO pin to a low state.
 * @param port The GPIO port (GPIO_PORT_A, GPIO_PORT_B, etc.).
 * @param pin_num The pin number (0-15).
 */
void gpio_clear_pin(gpio_port_t port, uint8_t pin_num);

/**
 * @brief Toggles the state of a GPIO pin.
 * @param port The GPIO port (GPIO_PORT_A, GPIO_PORT_B, etc.).
 * @param pin_num The pin number (0-15).
 */
void gpio_toggle_pin(gpio_port_t port, uint8_t pin_num);

/**
 * @brief Reads the state of a GPIO pin.
 * @param port The GPIO port (GPIO_PORT_A, GPIO_PORT_B, etc.).
 * @param pin_num The pin number (0-15).
 * @return The pin state (1 for high, 0 for low).
 */
uint8_t gpio_read_pin(gpio_port_t port, uint8_t pin_num);

/**
 * @brief Sets the alternate function for a GPIO pin.
 *
 * Configures the AFR (Alternate Function Register) for the given pin.
 * The pin must already be configured in AF mode via gpio_configure_pin().
 *
 * @param port The GPIO port (GPIO_PORT_A, GPIO_PORT_B, etc.).
 * @param pin_num The pin number (0-15).
 * @param af The alternate function number (0-15).
 */
void gpio_set_af(gpio_port_t port, uint8_t pin_num, uint8_t af);

#endif /* GPIO_HANDLER_H_ */
