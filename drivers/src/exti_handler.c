#include <stdint.h>

#include "exti_handler.h"
#include "gpio_handler.h" 
#include "stm32f4xx.h"

/* Private helper functions */
static int is_valid_pin(uint8_t pin_num);
static int is_valid_exti_line(uint8_t line);
static int is_valid_gpio_port(gpio_port_t port);
static uint8_t get_port_value(gpio_port_t port);
static IRQn_Type get_exti_irq_number(uint8_t line);
static void configure_syscfg_exti_port(uint8_t line, gpio_port_t port);

/* Implementation of public functions */

int exti_configure_gpio_interrupt(gpio_port_t port, uint8_t pin_num, 
                                  exti_trigger_t trigger, exti_mode_t mode)
{
    /* Parameter validation */
    if (!is_valid_gpio_port(port) || !is_valid_pin(pin_num) || 
        trigger >= EXTI_TRIGGER_INVALID || mode >= EXTI_MODE_INVALID) {
        return -1;
    }

    /* __get_PRIMASK() returns 0 if interrupts are enabled, non-zero if disabled */
    uint32_t interrupts_enabled = (__get_PRIMASK() == 0);

    /* Disable global interrupts during configuration */
    __disable_irq();

    /* Enable GPIO clock for the port using gpio_handler */
    gpio_clock_enable(port);

    /* Configure GPIO pin as input using gpio_handler */
    gpio_configure_pin(port, pin_num, GPIO_MODE_INPUT);

    /* Enable SYSCFG clock */
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;

    /* Configure SYSCFG EXTI port selection */
    configure_syscfg_exti_port(pin_num, port);

    /* Configure trigger type */
    if (trigger == EXTI_TRIGGER_RISING || trigger == EXTI_TRIGGER_BOTH) {
        EXTI->RTSR |= (1U << pin_num);
    } else {
        EXTI->RTSR &= ~(1U << pin_num);
    }

    if (trigger == EXTI_TRIGGER_FALLING || trigger == EXTI_TRIGGER_BOTH) {
        EXTI->FTSR |= (1U << pin_num);
    } else {
        EXTI->FTSR &= ~(1U << pin_num);
    }

    /* Configure mode (interrupt and/or event) */
    if (mode == EXTI_MODE_INTERRUPT || mode == EXTI_MODE_BOTH) {
        EXTI->IMR |= (1U << pin_num);
    } else {
        EXTI->IMR &= ~(1U << pin_num);
    }

    if (mode == EXTI_MODE_EVENT || mode == EXTI_MODE_BOTH) {
        EXTI->EMR |= (1U << pin_num);
    } else {
        EXTI->EMR &= ~(1U << pin_num);
    }

    /* Enable NVIC interrupt */
    IRQn_Type irq_num = get_exti_irq_number(pin_num);
    if (irq_num != (IRQn_Type)-1) {
        NVIC_EnableIRQ(irq_num);
    }

    /* Restore the original global interrupt state */
    if (interrupts_enabled) {
        __enable_irq();
    }

    return 0;
}

int exti_enable_line(uint8_t line)
{
    if (!is_valid_exti_line(line)) {
        return -1;
    }

    IRQn_Type irq_num = get_exti_irq_number(line);
    if (irq_num != (IRQn_Type)-1) {
        NVIC_EnableIRQ(irq_num);
        return 0;
    }
    return -1;
}

int exti_disable_line(uint8_t line)
{
    if (!is_valid_exti_line(line)) {
        return -1;
    }

    IRQn_Type irq_num = get_exti_irq_number(line);
    if (irq_num != (IRQn_Type)-1) {
        NVIC_DisableIRQ(irq_num);
        return 0;
    }
    return -1;
}

int exti_set_interrupt_mask(uint8_t line, uint8_t enable)
{
    if (!is_valid_exti_line(line)) {
        return -1;
    }

    if (enable) {
        EXTI->IMR |= (1U << line);
    } else {
        EXTI->IMR &= ~(1U << line);
    }
    return 0;
}

int exti_set_event_mask(uint8_t line, uint8_t enable)
{
    if (!is_valid_exti_line(line)) {
        return -1;
    }

    if (enable) {
        EXTI->EMR |= (1U << line);
    } else {
        EXTI->EMR &= ~(1U << line);
    }
    return 0;
}

int exti_is_pending(uint8_t line)
{
    if (!is_valid_exti_line(line)) {
        return -1;
    }

    return (EXTI->PR & (1U << line)) ? 1 : 0;
}

int exti_clear_pending(uint8_t line)
{
    if (!is_valid_exti_line(line)) {
        return -1;
    }

    /* Clear pending bit by writing 1 to it */
    EXTI->PR = (1U << line);
    return 0;
}

int exti_software_trigger(uint8_t line)
{
    if (!is_valid_exti_line(line)) {
        return -1;
    }

    /* Generate software interrupt by writing to SWIER */
    EXTI->SWIER |= (1U << line);
    return 0;
}

/* Private helper function implementations */

static int is_valid_pin(uint8_t pin_num)
{
    return (pin_num <= 15);
}

static int is_valid_exti_line(uint8_t line)
{
    return (line <= 22);  /* STM32F4 has EXTI lines 0-22 */
}

static int is_valid_gpio_port(gpio_port_t port)
{
    return (port >= GPIO_PORT_A && port <= GPIO_PORT_H);
}

static uint8_t get_port_value(gpio_port_t port)
{
    switch (port) {
        case GPIO_PORT_A: return 0;
        case GPIO_PORT_B: return 1;
        case GPIO_PORT_C: return 2;
        case GPIO_PORT_D: return 3;
        case GPIO_PORT_E: return 4;
        case GPIO_PORT_H: return 7;
        default: return 0xFF;  /* Invalid */
    }
}

static IRQn_Type get_exti_irq_number(uint8_t line)
{
    switch (line) {
        case 0:  return EXTI0_IRQn;
        case 1:  return EXTI1_IRQn;
        case 2:  return EXTI2_IRQn;
        case 3:  return EXTI3_IRQn;
        case 4:  return EXTI4_IRQn;
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:  return EXTI9_5_IRQn;
        case 10:
        case 11:
        case 12:
        case 13:
        case 14:
        case 15: return EXTI15_10_IRQn;
        default: return (IRQn_Type)-1;  /* Invalid */
    }
}

static void configure_syscfg_exti_port(uint8_t line, gpio_port_t port)
{
    uint8_t port_value = get_port_value(port);
    if (port_value == 0xFF) return;  /* Invalid port */

    /* Calculate which EXTICR register and bit position to use */
    uint8_t reg_index = line / 4;      /* EXTICR[0-3] */
    uint8_t bit_pos = (line % 4) * 4;  /* Bit position within register */

    /* Clear the 4 bits for this EXTI line */
    SYSCFG->EXTICR[reg_index] &= ~(0xF << bit_pos);
    
    /* Set the port value */
    SYSCFG->EXTICR[reg_index] |= (port_value << bit_pos);
}
