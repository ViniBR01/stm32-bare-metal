#include "shift_register.h"
#include "gpio_handler.h"
#include "stm32f4xx.h"

/* Pin definitions */
#define LATCH_PORT_ENUM GPIO_PORT_A
#define LATCH_PORT      GPIOA
#define LATCH_PIN       8U

#define SPI_PORT_ENUM   GPIO_PORT_B
#define SPI_PORT        GPIOB
#define SCK_PIN         3U   /* PB3 - SPI1_SCK */
#define MOSI_PIN        5U   /* PB5 - SPI1_MOSI */

/* GPIO Alternate Function */
#define GPIO_AF5_SPI1   0x05U

/* SPI baud rate prescaler (APB2 = 100MHz, /128 = ~781 kHz) */
#define SPI_BAUDRATE_PRESCALER_128  (0x6U << 3)  /* BR[2:0] = 110 */

void shift_register_init(void)
{
    /* Enable GPIO clocks using gpio_handler */
    gpio_clock_enable(LATCH_PORT_ENUM);
    gpio_clock_enable(SPI_PORT_ENUM);
    
    /* Enable SPI1 peripheral clock */
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;

    /* Configure latch pin (PA8) as output using gpio_handler */
    gpio_configure_pin(LATCH_PORT_ENUM, LATCH_PIN, GPIO_MODE_OUTPUT);
    
    /* Configure additional GPIO settings for latch pin (not covered by gpio_handler) */
    LATCH_PORT->OTYPER &= ~(0x1U << LATCH_PIN);           /* Push-pull */
    LATCH_PORT->OSPEEDR |= (0x3U << (LATCH_PIN * 2));     /* High speed */
    LATCH_PORT->PUPDR &= ~(0x3U << (LATCH_PIN * 2));      /* No pull-up/pull-down */
    
    /* Set latch LOW initially using gpio_handler */
    gpio_clear_pin(LATCH_PORT_ENUM, LATCH_PIN);

    /* Configure SPI pins (PB3 = SCK, PB5 = MOSI) as alternate function using gpio_handler */
    gpio_configure_pin(SPI_PORT_ENUM, SCK_PIN, GPIO_MODE_AF);
    gpio_configure_pin(SPI_PORT_ENUM, MOSI_PIN, GPIO_MODE_AF);
    
    /* Configure additional GPIO settings for SCK pin (not covered by gpio_handler) */
    SPI_PORT->OTYPER &= ~(0x1U << SCK_PIN);               /* Push-pull */
    SPI_PORT->OSPEEDR |= (0x3U << (SCK_PIN * 2));         /* High speed */
    SPI_PORT->PUPDR &= ~(0x3U << (SCK_PIN * 2));          /* No pull-up/pull-down */
    
    /* Set alternate function to AF5 (SPI1) for PB3 */
    SPI_PORT->AFR[0] &= ~(0xFU << (SCK_PIN * 4));         /* Clear AF bits */
    SPI_PORT->AFR[0] |= (GPIO_AF5_SPI1 << (SCK_PIN * 4)); /* Set AF5 */

    /* Configure additional GPIO settings for MOSI pin (not covered by gpio_handler) */
    SPI_PORT->OTYPER &= ~(0x1U << MOSI_PIN);              /* Push-pull */
    SPI_PORT->OSPEEDR |= (0x3U << (MOSI_PIN * 2));        /* High speed */
    SPI_PORT->PUPDR &= ~(0x3U << (MOSI_PIN * 2));         /* No pull-up/pull-down */
    
    /* Set alternate function to AF5 (SPI1) for PB5 */
    SPI_PORT->AFR[0] &= ~(0xFU << (MOSI_PIN * 4));        /* Clear AF bits */
    SPI_PORT->AFR[0] |= (GPIO_AF5_SPI1 << (MOSI_PIN * 4));/* Set AF5 */

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
    gpio_clear_pin(LATCH_PORT_ENUM, LATCH_PIN);

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
    gpio_set_pin(LATCH_PORT_ENUM, LATCH_PIN);
}
