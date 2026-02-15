#include "spi_perf.h"
#include "gpio_handler.h"
#include "printf.h"

/* Only include hardware headers when compiling for target */
#ifndef SPI_PERF_HOST_TEST
#include "stm32f4xx.h"
#include "printf_dma.h"
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
static uint8_t tx_buf[SPI_PERF_MAX_BUF_SIZE];
static uint8_t rx_buf[SPI_PERF_MAX_BUF_SIZE];

/**
 * @brief Initialize SPI2 GPIO pins on Port B
 *
 * PB13 = SCK, PB14 = MISO, PB15 = MOSI (AF5)
 */
static void spi_perf_gpio_init(void) {
    gpio_clock_enable(GPIO_PORT_B);

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
 * @brief Initialize SPI2 as Master
 */
static void spi_perf_spi_init(uint16_t prescaler) {
    int br = spi_prescaler_to_br(prescaler);

    /* Enable SPI2 peripheral clock (APB1) */
    RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;

    /* Configure SPI2 as Master */
    /* Mode 0 (CPOL=0, CPHA=0), 8-bit, MSB first, SSM=1, SSI=1 */
    SPI2->CR1 = 0;
    SPI2->CR1 = SPI_CR1_MSTR          /* Master mode */
              | SPI_CR1_SSM           /* Software slave management */
              | SPI_CR1_SSI           /* Internal slave select high */
              | ((uint32_t)br << SPI_CR1_BR_Pos);  /* Baud rate */
}

/**
 * @brief Deinitialize SPI2 peripheral
 */
static void spi_perf_deinit(void) {
    SPI2->CR1 &= ~SPI_CR1_SPE;
    RCC->APB1ENR &= ~RCC_APB1ENR_SPI2EN;
}

/**
 * @brief Fill TX buffer with a known pattern and clear RX buffer
 */
static void spi_perf_fill_patterns(uint16_t size) {
    for (uint16_t i = 0; i < size; i++) {
        tx_buf[i] = (uint8_t)(i & 0xFF);
        rx_buf[i] = 0;
    }
}

/**
 * @brief Transmit data via SPI2 and capture what comes back on MISO
 * @return Elapsed DWT cycles
 */
static uint32_t spi_perf_transfer(uint16_t size) {
    /* Enable DWT cycle counter */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* Enable SPI2 */
    SPI2->CR1 |= SPI_CR1_SPE;

    for (uint16_t i = 0; i < size; i++) {
        /* Wait for TX buffer empty, then write */
        while (!(SPI2->SR & SPI_SR_TXE));
        SPI2->DR = tx_buf[i];

        /* Wait for RX buffer not empty, then read */
        while (!(SPI2->SR & SPI_SR_RXNE));
        rx_buf[i] = (uint8_t)(SPI2->DR & 0xFF);
    }

    /* Wait until not busy */
    while (SPI2->SR & SPI_SR_BSY);

    uint32_t cycles = DWT->CYCCNT;

    SPI2->CR1 &= ~SPI_CR1_SPE;

    return cycles;
}

/**
 * @brief Print a byte buffer, truncating with "..." if longer than 8 entries
 *
 * Prints all entries when size <= 8, otherwise prints the first 4 and last 4
 * with a " .. " separator to indicate omitted entries.
 */
static void spi_perf_print_buf(const uint8_t *buf, uint16_t size) {
    if (size <= 8) {
        for (uint16_t i = 0; i < size; i++) {
            printf(" 0x%02X", buf[i]);
        }
    } else {
        for (uint16_t i = 0; i < 4; i++) {
            printf(" 0x%02X", buf[i]);
        }
        printf(" ..");
        for (uint16_t i = size - 4; i < size; i++) {
            printf(" 0x%02X", buf[i]);
        }
    }
}

int spi_perf_run(uint16_t prescaler, uint16_t buffer_size) {
    uint32_t spi_freq_hz = SPI_PERF_APB1_CLOCK_HZ / prescaler;
    uint32_t spi_freq_khz = spi_freq_hz / 1000;

    printf("--- SPI2 Master TX Test ---\n");
    if (spi_freq_khz >= 1000) {
        printf("  Clock:  %lu MHz (prescaler %u)\n", spi_freq_khz / 1000, prescaler);
    } else {
        printf("  Clock:  %lu kHz (prescaler %u)\n", spi_freq_khz, prescaler);
    }
    printf("  Bytes:  %u\n", buffer_size);
    printf("  Peak Tput:   %lu KB/s\n", spi_freq_hz / 8000);

    /* Flush header output before running transfer */
    printf_dma_flush();

    /* Initialize hardware */
    spi_perf_gpio_init();
    spi_perf_spi_init(prescaler);

    /* Fill test patterns */
    spi_perf_fill_patterns(buffer_size);

    /* Run transfer and measure */
    uint32_t cycles = spi_perf_transfer(buffer_size);

    /* Compute timing */
    uint32_t clock_mhz = SPI_PERF_APB1_CLOCK_HZ / 1000000;
    uint32_t elapsed_us = cycles / clock_mhz;

    /* Compute throughput: (bytes * 1000000) / elapsed_us = bytes/s */
    uint32_t throughput_kbps = 0;
    if (elapsed_us > 0) {
        throughput_kbps = ((uint32_t)buffer_size * 1000u) / elapsed_us;
    }

    /* Compare TX and RX buffers */
    uint16_t match_count = 0;
    for (uint16_t i = 0; i < buffer_size; i++) {
        if (tx_buf[i] == rx_buf[i]) {
            match_count++;
        }
    }

    /* Print results */
    printf("--- Results ---\n");
    printf("  Cycles: %lu\n", cycles);
    printf("  Time:   %lu us\n", elapsed_us);
    printf("  Thpt:   %lu KB/s\n", throughput_kbps);

    printf_dma_flush();

    printf("  TX:");
    spi_perf_print_buf(tx_buf, buffer_size);
    printf("\n");

    printf("  RX:");
    spi_perf_print_buf(rx_buf, buffer_size);
    printf("\n");

    printf("  Match:  %u/%u", match_count, buffer_size);
    if (match_count == buffer_size) {
        printf(" (OK)\n");
    } else {
        printf(" (FAIL - %u errors)\n", buffer_size - match_count);
    }
    printf("---------------------------\n");

    /* Cleanup */
    spi_perf_deinit();

    return 0;
}

#endif /* SPI_PERF_HOST_TEST */
