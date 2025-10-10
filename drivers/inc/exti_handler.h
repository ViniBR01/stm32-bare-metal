#ifndef EXTI_HANDLER_H_
#define EXTI_HANDLER_H_

#include <stdint.h>
#include "gpio_handler.h"  // For gpio_port_t enum

/**
 * @brief EXTI trigger types
 */
typedef enum {
    EXTI_TRIGGER_RISING = 0,
    EXTI_TRIGGER_FALLING,
    EXTI_TRIGGER_BOTH,
    EXTI_TRIGGER_INVALID
} exti_trigger_t;

/**
 * @brief EXTI mode (interrupt vs event)
 */
typedef enum {
    EXTI_MODE_INTERRUPT = 0,
    EXTI_MODE_EVENT,
    EXTI_MODE_BOTH,
    EXTI_MODE_INVALID
} exti_mode_t;

/**
 * @brief Configure GPIO pin as EXTI source and enable interrupt
 * @param port GPIO port (GPIO_PORT_A, GPIO_PORT_B, etc.)
 * @param pin_num Pin number (0-15)
 * EXTI line is automatically selected (should match pin_num for GPIO)
 * @param trigger Trigger type (rising, falling, both)
 * @param mode EXTI mode (interrupt, event, both)
 * @return 0 on success, -1 on error
 */
int exti_configure_gpio_interrupt(gpio_port_t port, uint8_t pin_num, 
                                  exti_trigger_t trigger, exti_mode_t mode);

/**
 * @brief Enable EXTI line in NVIC
 * @param line EXTI line to enable
 * @return 0 on success, -1 on error
 */
int exti_enable_line(uint8_t line);

/**
 * @brief Disable EXTI line in NVIC
 * @param line EXTI line to disable
 * @return 0 on success, -1 on error
 */
int exti_disable_line(uint8_t line);

/**
 * @brief Enable/disable EXTI line interrupt mask
 * @param line EXTI line
 * @param enable true to enable, false to disable
 * @return 0 on success, -1 on error
 */
int exti_set_interrupt_mask(uint8_t line, uint8_t enable);

/**
 * @brief Enable/disable EXTI line event mask
 * @param line EXTI line
 * @param enable true to enable, false to disable
 * @return 0 on success, -1 on error
 */
int exti_set_event_mask(uint8_t line, uint8_t enable);

/**
 * @brief Check if EXTI line interrupt is pending
 * @param line EXTI line to check
 * @return 1 if pending, 0 if not pending, -1 on error
 */
int exti_is_pending(uint8_t line);

/**
 * @brief Clear EXTI line pending flag
 * @param line EXTI line to clear
 * @return 0 on success, -1 on error
 */
int exti_clear_pending(uint8_t line);

/**
 * @brief Disable EXTI line completely
 * @param line EXTI line to disable
 * @return 0 on success, -1 on error
 */
int exti_disable_line(uint8_t line);

/**
 * @brief Generate software interrupt on EXTI line
 * @param line EXTI line
 * @return 0 on success, -1 on error
 */
int exti_software_trigger(uint8_t line);

#endif /* EXTI_HANDLER_H_ */