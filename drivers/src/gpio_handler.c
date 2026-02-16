#include "gpio_handler.h"
#include "stm32f4xx.h"

static GPIO_TypeDef* get_gpio_port_ptr(gpio_port_t port)
{
    switch (port)
    {
        case GPIO_PORT_A: return GPIOA;
        case GPIO_PORT_B: return GPIOB;
        case GPIO_PORT_C: return GPIOC;
        case GPIO_PORT_D: return GPIOD;
        case GPIO_PORT_E: return GPIOE;
        case GPIO_PORT_H: return GPIOH;
        default:          return 0;
    }
}

void gpio_clock_enable(gpio_port_t port)
{
    uint32_t rcc_ahb1enr_bit = 0;
    switch (port)
    {
        case GPIO_PORT_A: rcc_ahb1enr_bit = RCC_AHB1ENR_GPIOAEN; break;
        case GPIO_PORT_B: rcc_ahb1enr_bit = RCC_AHB1ENR_GPIOBEN; break;
        case GPIO_PORT_C: rcc_ahb1enr_bit = RCC_AHB1ENR_GPIOCEN; break;
        case GPIO_PORT_D: rcc_ahb1enr_bit = RCC_AHB1ENR_GPIODEN; break;
        case GPIO_PORT_E: rcc_ahb1enr_bit = RCC_AHB1ENR_GPIOEEN; break;
        case GPIO_PORT_H: rcc_ahb1enr_bit = RCC_AHB1ENR_GPIOHEN; break;
        default:          return;
    }
    RCC->AHB1ENR |= rcc_ahb1enr_bit;
}

void gpio_clock_disable(gpio_port_t port)
{
    uint32_t rcc_ahb1enr_bit = 0;
    switch (port)
    {
        case GPIO_PORT_A: rcc_ahb1enr_bit = RCC_AHB1ENR_GPIOAEN; break;
        case GPIO_PORT_B: rcc_ahb1enr_bit = RCC_AHB1ENR_GPIOBEN; break;
        case GPIO_PORT_C: rcc_ahb1enr_bit = RCC_AHB1ENR_GPIOCEN; break;
        case GPIO_PORT_D: rcc_ahb1enr_bit = RCC_AHB1ENR_GPIODEN; break;
        case GPIO_PORT_E: rcc_ahb1enr_bit = RCC_AHB1ENR_GPIOEEN; break;
        case GPIO_PORT_H: rcc_ahb1enr_bit = RCC_AHB1ENR_GPIOHEN; break;
        default:          return;
    }
    RCC->AHB1ENR &= ~rcc_ahb1enr_bit;
}

void gpio_configure_pin(gpio_port_t port, uint8_t pin_num, gpio_mode_t mode)
{
    if (pin_num > 15 || mode < 0 || mode >= GPIO_MODE_INVALID) return;
    GPIO_TypeDef* gpio_port = get_gpio_port_ptr(port);
    if (!gpio_port) return;

    // Clear the two bits for the pin
    gpio_port->MODER &= ~(3U << (pin_num * 2));
    // Set the new mode, masking to 2 bits to prevent overflow
    gpio_port->MODER |= (((uint32_t)mode & 0x3) << (pin_num * 2));
}

void gpio_set_pin(gpio_port_t port, uint8_t pin_num)
{
    if (pin_num > 15) return;
    GPIO_TypeDef* gpio_port = get_gpio_port_ptr(port);
    if (!gpio_port) return;

    gpio_port->BSRR = (1U << pin_num);
}

void gpio_clear_pin(gpio_port_t port, uint8_t pin_num)
{
    if (pin_num > 15) return;
    GPIO_TypeDef* gpio_port = get_gpio_port_ptr(port);
    if (!gpio_port) return;

    gpio_port->BSRR = (1U << (pin_num + 16));
}

void gpio_toggle_pin(gpio_port_t port, uint8_t pin_num)
{
    if (pin_num > 15) return;
    GPIO_TypeDef* gpio_port = get_gpio_port_ptr(port);
    if (!gpio_port) return;

    gpio_port->ODR ^= (1U << pin_num);
}

uint8_t gpio_read_pin(gpio_port_t port, uint8_t pin_num)
{
    if (pin_num > 15) return 0;
    GPIO_TypeDef* gpio_port = get_gpio_port_ptr(port);
    if (!gpio_port) return 0;

    return (gpio_port->IDR & (1U << pin_num)) ? 1 : 0;
}

void gpio_set_af(gpio_port_t port, uint8_t pin_num, uint8_t af)
{
    if (pin_num > 15 || af > 15) return;
    GPIO_TypeDef* gpio = get_gpio_port_ptr(port);
    if (!gpio) return;

    uint8_t reg = pin_num / 8;       /* AFR[0] for pins 0-7, AFR[1] for 8-15 */
    uint8_t pos = (pin_num % 8) * 4; /* 4 bits per pin within the register */
    gpio->AFR[reg] &= ~(0xFU << pos);
    gpio->AFR[reg] |= ((uint32_t)af & 0xFU) << pos;
}
