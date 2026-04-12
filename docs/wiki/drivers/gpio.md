# Driver: GPIO

**Files:** `drivers/inc/gpio_handler.h`, `drivers/src/gpio_handler.c`

## Purpose

Abstracts STM32F411 GPIO port/pin configuration and I/O operations. Used by all other drivers that need GPIO pins (UART, SPI, EXTI).

## API

```c
// Clock
void gpio_clock_enable(gpio_port_t port);
void gpio_clock_disable(gpio_port_t port);

// Pin configuration (individual fields)
void gpio_configure_pin(gpio_port_t port, uint8_t pin_num, gpio_mode_t mode);
void gpio_set_output_type(gpio_port_t port, uint8_t pin_num, gpio_output_type_t type);
void gpio_set_speed(gpio_port_t port, uint8_t pin_num, gpio_speed_t speed);
void gpio_set_pull(gpio_port_t port, uint8_t pin_num, gpio_pull_t pull);
void gpio_set_af(gpio_port_t port, uint8_t pin_num, uint8_t af);

// Convenience: configure mode + output type + speed + pull in one call
void gpio_configure_full(gpio_port_t port, uint8_t pin_num, gpio_mode_t mode,
                         gpio_output_type_t output_type, gpio_speed_t speed,
                         gpio_pull_t pull);

// I/O
void    gpio_set_pin(gpio_port_t port, uint8_t pin_num);
void    gpio_clear_pin(gpio_port_t port, uint8_t pin_num);
void    gpio_toggle_pin(gpio_port_t port, uint8_t pin_num);
uint8_t gpio_read_pin(gpio_port_t port, uint8_t pin_num);
```

## Types

| Type | Values |
|---|---|
| `gpio_port_t` | `GPIO_PORT_A` .. `GPIO_PORT_H` |
| `gpio_mode_t` | `GPIO_MODE_INPUT`, `OUTPUT`, `AF`, `ANALOG` |
| `gpio_output_type_t` | `GPIO_OUTPUT_PUSH_PULL`, `OPEN_DRAIN` |
| `gpio_speed_t` | `GPIO_SPEED_LOW`, `MEDIUM`, `FAST`, `HIGH` |
| `gpio_pull_t` | `GPIO_PULL_NONE`, `PULL_UP`, `PULL_DOWN` |

## Usage pattern

```c
gpio_clock_enable(GPIO_PORT_A);
gpio_configure_full(GPIO_PORT_A, 5, GPIO_MODE_OUTPUT,
                    GPIO_OUTPUT_PUSH_PULL, GPIO_SPEED_LOW, GPIO_PULL_NONE);
gpio_set_pin(GPIO_PORT_A, 5);    // LED on
gpio_clear_pin(GPIO_PORT_A, 5);  // LED off
gpio_toggle_pin(GPIO_PORT_A, 5); // toggle
```

For alternate function (e.g. SPI, UART):
```c
gpio_clock_enable(GPIO_PORT_A);
gpio_configure_full(GPIO_PORT_A, 2, GPIO_MODE_AF,
                    GPIO_OUTPUT_PUSH_PULL, GPIO_SPEED_HIGH, GPIO_PULL_NONE);
gpio_set_af(GPIO_PORT_A, 2, 7);  // AF7 = USART2 TX on PA2
```
