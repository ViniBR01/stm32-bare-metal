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
 * @brief GPIO output type (push-pull vs open-drain)
 */
typedef enum {
    GPIO_OUTPUT_PUSH_PULL = 0,
    GPIO_OUTPUT_OPEN_DRAIN
} gpio_output_type_t;

/**
 * @brief GPIO output speed (slew rate)
 */
typedef enum {
    GPIO_SPEED_LOW = 0,
    GPIO_SPEED_MEDIUM,
    GPIO_SPEED_FAST,
    GPIO_SPEED_HIGH
} gpio_speed_t;

/**
 * @brief GPIO pull-up / pull-down configuration
 */
typedef enum {
    GPIO_PULL_NONE = 0,
    GPIO_PULL_UP,
    GPIO_PULL_DOWN
} gpio_pull_t;

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

/**
 * @brief Sets the output type for a GPIO pin (GPIOx->OTYPER).
 * @param port The GPIO port (GPIO_PORT_A, GPIO_PORT_B, etc.).
 * @param pin_num The pin number (0-15).
 * @param type Push-pull or open-drain.
 */
void gpio_set_output_type(gpio_port_t port, uint8_t pin_num, gpio_output_type_t type);

/**
 * @brief Sets the output speed (slew rate) for a GPIO pin (GPIOx->OSPEEDR).
 * @param port The GPIO port (GPIO_PORT_A, GPIO_PORT_B, etc.).
 * @param pin_num The pin number (0-15).
 * @param speed Low, medium, fast, or high.
 */
void gpio_set_speed(gpio_port_t port, uint8_t pin_num, gpio_speed_t speed);

/**
 * @brief Sets the pull-up/pull-down resistor for a GPIO pin (GPIOx->PUPDR).
 * @param port The GPIO port (GPIO_PORT_A, GPIO_PORT_B, etc.).
 * @param pin_num The pin number (0-15).
 * @param pull None, pull-up, or pull-down.
 */
void gpio_set_pull(gpio_port_t port, uint8_t pin_num, gpio_pull_t pull);

/**
 * @brief Fully configures a GPIO pin: mode, output type, speed, and pull.
 * @param port The GPIO port (GPIO_PORT_A, GPIO_PORT_B, etc.).
 * @param pin_num The pin number (0-15).
 * @param mode The pin mode (input, output, AF, analog).
 * @param output_type Push-pull or open-drain.
 * @param speed Low, medium, fast, or high.
 * @param pull None, pull-up, or pull-down.
 */
void gpio_configure_full(gpio_port_t port, uint8_t pin_num, gpio_mode_t mode,
                         gpio_output_type_t output_type, gpio_speed_t speed,
                         gpio_pull_t pull);

#endif /* GPIO_HANDLER_H_ */
