#include "shift_register.h"
#include "gpio_handler.h"
#include "stm32f4xx.h"

/* Pin definitions */
#define LATCH_PORT      GPIO_PORT_A
#define LATCH_PIN       8U

#define SPI_PORT        GPIO_PORT_B
#define SCK_PIN         3U   /* PB3 - SPI1_SCK */
#define MOSI_PIN        5U   /* PB5 - SPI1_MOSI */

/* GPIO Alternate Function */
#define GPIO_AF5_SPI1   0x05U

/* SPI baud rate prescaler (APB2 = 100MHz, /128 = ~781 kHz) */
#define SPI_BAUDRATE_PRESCALER_128  (0x6U << 3)  /* BR[2:0] = 110 */

void shift_register_init(void)
{
    /* Enable GPIO clocks using gpio_handler */
    gpio_clock_enable(LATCH_PORT);
    gpio_clock_enable(SPI_PORT);
    
    /* Enable SPI1 peripheral clock */
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;

    /* Configure latch pin (PA8) as push-pull output, high speed, no pull */
    gpio_configure_full(LATCH_PORT, LATCH_PIN, GPIO_MODE_OUTPUT,
                        GPIO_OUTPUT_PUSH_PULL, GPIO_SPEED_HIGH, GPIO_PULL_NONE);

    /* Set latch LOW initially */
    gpio_clear_pin(LATCH_PORT, LATCH_PIN);

    /* Configure SCK (PB3) as AF push-pull, high speed, no pull */
    gpio_configure_full(SPI_PORT, SCK_PIN, GPIO_MODE_AF,
                        GPIO_OUTPUT_PUSH_PULL, GPIO_SPEED_HIGH, GPIO_PULL_NONE);
    gpio_set_af(SPI_PORT, SCK_PIN, GPIO_AF5_SPI1);

    /* Configure MOSI (PB5) as AF push-pull, high speed, no pull */
    gpio_configure_full(SPI_PORT, MOSI_PIN, GPIO_MODE_AF,
                        GPIO_OUTPUT_PUSH_PULL, GPIO_SPEED_HIGH, GPIO_PULL_NONE);
    gpio_set_af(SPI_PORT, MOSI_PIN, GPIO_AF5_SPI1);

    /* Configure SPI1 */
    /* SPI must be disabled before configuration */
    SPI1->CR1 &= ~SPI_CR1_SPE;

    /* 
     * CR1 Configuration:
     * - CPHA = 0 (first clock transition is the first data capture edge)
     * - CPOL = 0 (clock is LOW when idle)
     * - MSTR = 1 (master mode)
     * - BR[2:0] = 110 (fPCLK/128, ~781 kHz)
     * - SPE = 0 (disabled for now)
     * - LSBFIRST = 0 (MSB first)
     * - SSI = 1 (internal slave select)
     * - SSM = 1 (software slave management)
     * - RXONLY = 0 (full duplex)
     * - DFF = 0 (8-bit data frame)
     */
    SPI1->CR1 = SPI_CR1_MSTR |              /* Master mode */
                SPI_CR1_SSM |               /* Software slave management */
                SPI_CR1_SSI |               /* Internal slave select high */
                SPI_BAUDRATE_PRESCALER_128; /* Baud rate /128 */

    /* CR2 Configuration: Keep default (all zeros) */
    SPI1->CR2 = 0;

    /* Enable SPI1 */
    SPI1->CR1 |= SPI_CR1_SPE;
}

void shift_register_write(uint8_t data)
{
    /* Set latch LOW to prepare for data shift using gpio_handler */
    gpio_clear_pin(LATCH_PORT, LATCH_PIN);

    /* Wait for transmit buffer to be empty */
    while (!(SPI1->SR & SPI_SR_TXE))
    {
        /* Wait */
    }

    /* Send data */
    SPI1->DR = data;

    /* Wait for transmit buffer to be empty (data moved to shift register) */
    while (!(SPI1->SR & SPI_SR_TXE))
    {
        /* Wait */
    }

    /* Wait for SPI to finish transmission (BSY flag clear) */
    while (SPI1->SR & SPI_SR_BSY)
    {
        /* Wait */
    }

    /* Set latch HIGH to transfer shift register data to output register using gpio_handler */
    gpio_set_pin(LATCH_PORT, LATCH_PIN);
}
