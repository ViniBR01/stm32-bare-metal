#include "spi_perf.h"
#include "gpio_handler.h"
#include "printf.h"

/* Only include hardware headers when compiling for target */
#ifndef SPI_PERF_HOST_TEST
#include "stm32f4xx.h"
#endif

/*===========================================================================
 * Pure logic functions (no hardware dependencies, testable on host)
 *===========================================================================*/

int spi_prescaler_to_br(uint16_t prescaler) {
    switch (prescaler) {
        case 2:   return 0;
        case 4:   return 1;
        case 8:   return 2;
        case 16:  return 3;
        case 32:  return 4;
        case 64:  return 5;
        case 128: return 6;
        case 256: return 7;
        default:  return -1;
    }
}

/**
 * @brief Simple string-to-unsigned-int parser
 * @param s Input string
 * @param out Output value
 * @return Pointer past the parsed digits, or NULL on error
 */
static const char* parse_uint(const char* s, uint32_t* out) {
    if (*s < '0' || *s > '9') return 0;
    uint32_t val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (uint32_t)(*s - '0');
        s++;
    }
    *out = val;
    return s;
}

spi_perf_args_t spi_perf_parse_args(const char* args) {
    spi_perf_args_t result;
    result.prescaler = SPI_PERF_DEFAULT_PRESCALER;
    result.buffer_size = SPI_PERF_DEFAULT_BUF_SIZE;
    result.error = 0;

    /* Skip leading whitespace */
    while (*args == ' ') args++;

    /* Empty args -> use defaults */
    if (*args == '\0') return result;

    /* Parse prescaler */
    uint32_t val = 0;
    const char* p = parse_uint(args, &val);
    if (!p) { result.error = 1; return result; }
    result.prescaler = (uint16_t)val;

    /* Skip whitespace */
    while (*p == ' ') p++;

    /* Parse optional buffer_size */
    if (*p != '\0') {
        val = 0;
        p = parse_uint(p, &val);
        if (!p) { result.error = 1; return result; }
        result.buffer_size = (uint16_t)val;
    }

    /* Validate prescaler */
    if (spi_prescaler_to_br(result.prescaler) < 0) {
        result.error = 1;
        return result;
    }

    /* Validate buffer_size */
    if (result.buffer_size == 0 || result.buffer_size > SPI_PERF_MAX_BUF_SIZE) {
        result.error = 1;
        return result;
    }

    return result;
}

/*===========================================================================
 * Hardware-dependent code (excluded from host tests)
 *===========================================================================*/
#ifndef SPI_PERF_HOST_TEST

/* Static buffers for SPI transfer */
static uint8_t master_tx[SPI_PERF_MAX_BUF_SIZE];
static uint8_t master_rx[SPI_PERF_MAX_BUF_SIZE];
static uint8_t slave_tx[SPI_PERF_MAX_BUF_SIZE];
static uint8_t slave_rx[SPI_PERF_MAX_BUF_SIZE];

/**
 * @brief Initialize SPI1 (Master) and SPI2 (Slave) GPIO and clocks
 */
static void spi_perf_gpio_init(void) {
    /* Enable GPIO clocks for Port A (SPI1) and Port B (SPI2) */
    gpio_clock_enable(GPIO_PORT_A);
    gpio_clock_enable(GPIO_PORT_B);

    /* Configure PA5 (SCK), PA6 (MISO), PA7 (MOSI) as AF mode */
    gpio_configure_pin(GPIO_PORT_A, 5, GPIO_MODE_AF);
    gpio_configure_pin(GPIO_PORT_A, 6, GPIO_MODE_AF);
    gpio_configure_pin(GPIO_PORT_A, 7, GPIO_MODE_AF);

    /* Set AF5 for PA5, PA6, PA7 (SPI1) */
    /* PA5 = AFR[0] bits [23:20], PA6 = bits [27:24], PA7 = bits [31:28] */
    GPIOA->AFR[0] &= ~(0xFFFU << 20);
    GPIOA->AFR[0] |=  (0x555U << 20);  /* AF5 for pins 5,6,7 */

    /* Configure PB13 (SCK), PB14 (MISO), PB15 (MOSI) as AF mode */
    gpio_configure_pin(GPIO_PORT_B, 13, GPIO_MODE_AF);
    gpio_configure_pin(GPIO_PORT_B, 14, GPIO_MODE_AF);
    gpio_configure_pin(GPIO_PORT_B, 15, GPIO_MODE_AF);

    /* Set AF5 for PB13, PB14, PB15 (SPI2) */
    /* PB13 = AFR[1] bits [23:20], PB14 = bits [27:24], PB15 = bits [31:28] */
    GPIOB->AFR[1] &= ~(0xFFFU << 20);
    GPIOB->AFR[1] |=  (0x555U << 20);  /* AF5 for pins 13,14,15 */
}

/**
 * @brief Initialize SPI1 as Master and SPI2 as Slave
 */
static void spi_perf_spi_init(uint16_t prescaler) {
    int br = spi_prescaler_to_br(prescaler);

    /* Enable SPI peripheral clocks */
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;  /* SPI1 on APB2 */
    RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;  /* SPI2 on APB1 */

    /* Configure SPI1 as Master */
    /* Mode 0 (CPOL=0, CPHA=0), 8-bit, MSB first, SSM=1, SSI=1 */
    SPI1->CR1 = 0;
    SPI1->CR1 = SPI_CR1_MSTR          /* Master mode */
              | SPI_CR1_SSM           /* Software slave management */
              | SPI_CR1_SSI           /* Internal slave select high */
              | ((uint32_t)br << SPI_CR1_BR_Pos);  /* Baud rate */
    /* CPOL=0, CPHA=0, DFF=0 (8-bit), LSBFIRST=0 are all 0 by default */

    /* Configure SPI2 as Slave */
    /* Mode 0, 8-bit, MSB first, SSM=1, SSI=0 (slave) */
    SPI2->CR1 = 0;
    SPI2->CR1 = SPI_CR1_SSM;          /* Software slave management, SSI=0 */
}

/**
 * @brief Deinitialize SPI peripherals
 */
static void spi_perf_deinit(void) {
    /* Disable SPI peripherals */
    SPI1->CR1 &= ~SPI_CR1_SPE;
    SPI2->CR1 &= ~SPI_CR1_SPE;

    /* Disable SPI clocks */
    RCC->APB2ENR &= ~RCC_APB2ENR_SPI1EN;
    RCC->APB1ENR &= ~RCC_APB1ENR_SPI2EN;
}

/**
 * @brief Fill TX buffers with known patterns
 */
static void spi_perf_fill_patterns(uint16_t size) {
    for (uint16_t i = 0; i < size; i++) {
        master_tx[i] = (uint8_t)(i & 0xFF);
        slave_tx[i]  = (uint8_t)(0xFF - (i & 0xFF));
        master_rx[i] = 0;
        slave_rx[i]  = 0;
    }
}

/**
 * @brief Perform full-duplex SPI transfer and return elapsed DWT cycles
 */
static uint32_t spi_perf_transfer(uint16_t size) {
    /* Prime slave: enable SPI2 and load first byte */
    SPI2->CR1 |= SPI_CR1_SPE;
    SPI2->DR = slave_tx[0];

    /* Enable DWT cycle counter */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* Enable SPI1 (master starts clocking) */
    SPI1->CR1 |= SPI_CR1_SPE;

    /* Transfer loop */
    for (uint16_t i = 0; i < size; i++) {
        /* Master: wait TXE, write byte */
        while (!(SPI1->SR & SPI_SR_TXE));
        SPI1->DR = master_tx[i];

        /* Master: wait RXNE, read byte */
        while (!(SPI1->SR & SPI_SR_RXNE));
        master_rx[i] = (uint8_t)(SPI1->DR & 0xFF);

        /* Slave: wait RXNE, read byte */
        while (!(SPI2->SR & SPI_SR_RXNE));
        slave_rx[i] = (uint8_t)(SPI2->DR & 0xFF);

        /* Slave: load next byte for next exchange */
        if (i + 1 < size) {
            while (!(SPI2->SR & SPI_SR_TXE));
            SPI2->DR = slave_tx[i + 1];
        }
    }

    /* Capture elapsed cycles */
    uint32_t cycles = DWT->CYCCNT;

    /* Disable SPI peripherals */
    SPI1->CR1 &= ~SPI_CR1_SPE;
    SPI2->CR1 &= ~SPI_CR1_SPE;

    return cycles;
}

/**
 * @brief Verify data integrity in both directions
 * @return 0 = both pass, bit 0 = master integrity fail, bit 1 = slave integrity fail
 */
static int spi_perf_verify(uint16_t size) {
    int result = 0;

    /* Check master RX against slave TX pattern (slave data integrity) */
    for (uint16_t i = 0; i < size; i++) {
        if (master_rx[i] != slave_tx[i]) {
            result |= 1;  /* Slave data integrity fail */
            break;
        }
    }

    /* Check slave RX against master TX pattern (master data integrity) */
    for (uint16_t i = 0; i < size; i++) {
        if (slave_rx[i] != master_tx[i]) {
            result |= 2;  /* Master data integrity fail */
            break;
        }
    }

    return result;
}

int spi_perf_run(uint16_t prescaler, uint16_t buffer_size) {
    /* Compute SPI clock frequency for display */
    uint32_t spi_freq_hz = SPI_PERF_APB2_CLOCK_HZ / prescaler;
    uint32_t spi_freq_mhz = spi_freq_hz / 1000000;

    printf("Starting SPI Performance Test...\n");
    printf("Config: SPI1 (Master) -> SPI2 (Slave)\n");
    printf("Baud Rate Prescaler: %u (%lu MHz)\n", prescaler, spi_freq_mhz);
    printf("Buffer Size: %u Bytes\n", buffer_size);

    /* Initialize hardware */
    spi_perf_gpio_init();
    spi_perf_spi_init(prescaler);

    /* Fill test patterns */
    spi_perf_fill_patterns(buffer_size);

    /* Run transfer and measure */
    uint32_t cycles = spi_perf_transfer(buffer_size);

    /* Compute timing using integer math (no float support in printf) */
    /* elapsed_us = cycles / (clock_hz / 1000000) = cycles / 16 for 16MHz */
    uint32_t clock_mhz = SPI_PERF_APB2_CLOCK_HZ / 1000000;
    uint32_t elapsed_us = cycles / clock_mhz;
    uint32_t elapsed_ms_int = elapsed_us / 1000;
    uint32_t elapsed_ms_frac = (elapsed_us % 1000) / 10;  /* 2 decimal places */

    /* Throughput: bytes/sec = buffer_size * 1000000 / elapsed_us */
    /* MB/s = bytes/sec / (1024*1024) */
    /* To avoid overflow: compute in KB/s first, then convert */
    uint32_t throughput_kbps = 0;
    if (elapsed_us > 0) {
        throughput_kbps = ((uint32_t)buffer_size * 1000U) / elapsed_us;  /* KB/s approx */
    }
    uint32_t throughput_mbps_int = throughput_kbps / 1024;
    uint32_t throughput_mbps_frac = ((throughput_kbps % 1024) * 100) / 1024;

    /* Verify data integrity */
    int integrity = spi_perf_verify(buffer_size);

    /* Print results */
    printf("\n[Result]\n");
    printf("Time elapsed: %lu.%02lu ms\n", elapsed_ms_int, elapsed_ms_frac);
    printf("Throughput: %lu.%02lu MB/s\n", throughput_mbps_int, throughput_mbps_frac);
    printf("Master Data Integrity: %s\n", (integrity & 2) ? "FAIL" : "PASS");
    printf("Slave Data Integrity: %s\n",  (integrity & 1) ? "FAIL" : "PASS");

    /* Cleanup */
    spi_perf_deinit();

    return integrity;
}

#endif /* SPI_PERF_HOST_TEST */
