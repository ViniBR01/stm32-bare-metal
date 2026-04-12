# Driver: EXTI

**Files:** `drivers/inc/exti_handler.h`, `drivers/src/exti_handler.c`

## Purpose

Configures GPIO pins as external interrupt sources using the STM32F411 EXTI controller and SYSCFG EXTICR mux. Used by button interrupt and sleep-wakeup examples.

## API

```c
// Configure a GPIO pin as an EXTI interrupt source and enable it
int exti_configure_gpio_interrupt(gpio_port_t port, uint8_t pin_num,
                                  exti_trigger_t trigger, exti_mode_t mode);

// NVIC enable/disable
int exti_enable_line(uint8_t line);
int exti_disable_line(uint8_t line);

// Interrupt mask register
int exti_set_interrupt_mask(uint8_t line, uint8_t enable);
int exti_set_event_mask(uint8_t line, uint8_t enable);

// Pending flag
int exti_is_pending(uint8_t line);
int exti_clear_pending(uint8_t line);

// Software trigger
int exti_software_trigger(uint8_t line);
```

## Types

```c
typedef enum {
    EXTI_TRIGGER_RISING,
    EXTI_TRIGGER_FALLING,
    EXTI_TRIGGER_BOTH
} exti_trigger_t;

typedef enum {
    EXTI_MODE_INTERRUPT,
    EXTI_MODE_EVENT,
    EXTI_MODE_BOTH
} exti_mode_t;
```

## Notes

- EXTI line number maps directly to GPIO pin number (EXTI0 = pin 0, EXTI15 = pin 15).
- `exti_configure_gpio_interrupt` handles the SYSCFG EXTICR mux, trigger edge selection, mask register, and NVIC enable in one call.
- The actual ISR handler (e.g. `EXTI15_10_IRQHandler`) must be defined in application code; the driver does not register ISR handlers.
- `exti_clear_pending` must be called at the start of the ISR to prevent re-triggering.

## Usage pattern

```c
// Configure PC13 (user button on NUCLEO) as falling-edge interrupt
gpio_clock_enable(GPIO_PORT_C);
gpio_configure_pin(GPIO_PORT_C, 13, GPIO_MODE_INPUT);
exti_configure_gpio_interrupt(GPIO_PORT_C, 13,
                              EXTI_TRIGGER_FALLING,
                              EXTI_MODE_INTERRUPT);

// In the ISR:
void EXTI15_10_IRQHandler(void) {
    if (exti_is_pending(13)) {
        exti_clear_pending(13);
        // handle button press
    }
}
```
