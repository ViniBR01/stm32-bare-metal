#include "shift_register.h"
#include "log_c.h"
#include "log_platform.h"
#include "systick.h"

/**
 * @brief Shift register example - demonstrates SPI control of SN74HC595N
 * 
 * This example shows how to use the shift register driver to control
 * the SN74HC595N shift register IC via the SPI1 peripheral.
 * 
 * Hardware connections:
 * - PB5 (SPI1_MOSI) → DS (Serial Data Input, pin 14)
 * - PB3 (SPI1_SCK)  → SHCP (Shift Register Clock, pin 11)
 * - PA8 (GPIO)      → STCP (Storage Register Clock/Latch, pin 12)
 * - Connect 8 LEDs (with resistors) to outputs Q0-Q7 (pins 15, 1-7)
 * - VCC to pin 16, GND to pin 8
 * - MR (pin 10) to VCC (Master Reset, active LOW)
 * - OE (pin 13) to GND (Output Enable, active LOW)
 * 
 * The example counts from 0x00 to 0xFF in steps, with 100ms delay between
 * each value. The current value is logged to UART and displayed on the
 * shift register outputs.
 * 
 * Binary pattern visualization:
 * ...
 * 0x88 = 0b10001000 - Q7 and Q3 ON
 * 0x89 = 0b10001001 - Q7, Q3, Q0 ON
 * ...
 * 0xAA = 0b10101010 - Q7, Q5, Q3, Q1 ON (alternating pattern)
 * ...
 * 0xFF = 0b11111111 - All LEDs ON
 */

int main(void)
{
    /* Initialize logging with UART backend */
    log_platform_init_uart();
    
    /* Initialize the shift register driver */
    shift_register_init();
    
    /* Log startup message */
    loginfo("Shift Register Example Started");
    loginfo("Counting from 0x00 to 0xFF with 100ms steps");
    loginfo("Hardware: SN74HC595N via SPI1");
    
    /* Main loop - count from 0x88 to 0xAA */
    uint8_t value = 0x00;
    
    while (1)
    {
        /* Write current value to shift register */
        shift_register_write(value);
        
        /* Log the value */
        loginfo("Value: 0x%X", value);
        
        /* Delay 100ms */
        systick_delay_ms(100);
        
        /* Increment and wrap around */
        value++;
        if (value == 0xFF)
        {
            value = 0x00;
            loginfo("--- Wrapping back to 0x00 ---");
        }
    }
}

