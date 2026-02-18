#include "spi_perf.h"
#include "spi.h"
#include "printf.h"

/* Only include hardware headers when compiling for target */
#ifndef SPI_PERF_HOST_TEST
#include "stm32f4xx.h"
#include "printf_dma.h"
#endif

/*===========================================================================
 * Pure logic functions (no hardware dependencies, testable on host)
 *===========================================================================*/

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
    result.instance    = SPI_PERF_DEFAULT_INSTANCE;
    result.prescaler   = SPI_PERF_DEFAULT_PRESCALER;
    result.buffer_size = SPI_PERF_DEFAULT_BUF_SIZE;
    result.use_dma     = 0;
    result.error       = 0;

    /* Skip leading whitespace */
    while (*args == ' ') args++;

    /* Empty args -> use defaults */
    if (*args == '\0') return result;

    /* Parse SPI number (1-5) */
    uint32_t val = 0;
    const char* p = parse_uint(args, &val);
    if (!p) { result.error = 1; return result; }
    if (val < 1 || val > 5) { result.error = 1; return result; }
    result.instance = (spi_instance_t)(val - 1);

    /* Skip whitespace */
    while (*p == ' ') p++;
    if (*p == '\0') return result;

    /* Parse prescaler */
    val = 0;
    p = parse_uint(p, &val);
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

    /* Skip whitespace */
    while (*p == ' ') p++;

    /* Parse optional "dma" keyword */
    if (p[0] == 'd' && p[1] == 'm' && p[2] == 'a' &&
        (p[3] == '\0' || p[3] == ' ')) {
        result.use_dma = 1;
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

/**
 * @brief Default pin mappings for each SPI instance
 *
 * These are common Nucleo-F411RE (LQFP64) pin assignments.  The perf test
 * uses whichever entry matches the selected SPI instance.
 *
 * NOTE: SPI4 and SPI5 MISO pins (PA11, PA12) are shared with USB OTG FS
 * (D-, D+) on the Nucleo board.  Loopback tests on these instances may
 * fail if USB is active or board-level components interfere with the signals.
 */
static const spi_config_t spi_perf_pin_defaults[SPI_INSTANCE_COUNT] = {
    [SPI_INSTANCE_1] = {
        .instance  = SPI_INSTANCE_1,
        .sck_port  = GPIO_PORT_B, .sck_pin  = 3,   /* PB3  AF5 = SPI1_SCK  */
        .miso_port = GPIO_PORT_B, .miso_pin = 4,   /* PB4  AF5 = SPI1_MISO */
        .mosi_port = GPIO_PORT_B, .mosi_pin = 5,   /* PB5  AF5 = SPI1_MOSI */
        .sck_af = 5, .miso_af = 5, .mosi_af = 5,
    },
    [SPI_INSTANCE_2] = {
        .instance  = SPI_INSTANCE_2,
        .sck_port  = GPIO_PORT_B, .sck_pin  = 13,  /* PB13 AF5 = SPI2_SCK  */
        .miso_port = GPIO_PORT_B, .miso_pin = 14,  /* PB14 AF5 = SPI2_MISO */
        .mosi_port = GPIO_PORT_B, .mosi_pin = 15,  /* PB15 AF5 = SPI2_MOSI */
        .sck_af = 5, .miso_af = 5, .mosi_af = 5,
    },
    [SPI_INSTANCE_3] = {
        .instance  = SPI_INSTANCE_3,
        .sck_port  = GPIO_PORT_C, .sck_pin  = 10,  /* PC10 AF6 = SPI3_SCK  */
        .miso_port = GPIO_PORT_C, .miso_pin = 11,  /* PC11 AF6 = SPI3_MISO */
        .mosi_port = GPIO_PORT_C, .mosi_pin = 12,  /* PC12 AF6 = SPI3_MOSI */
        .sck_af = 6, .miso_af = 6, .mosi_af = 6,
    },
    [SPI_INSTANCE_4] = {
        .instance  = SPI_INSTANCE_4,
        .sck_port  = GPIO_PORT_B, .sck_pin  = 13,  /* PB13 AF6 = SPI4_SCK  */
        .miso_port = GPIO_PORT_A, .miso_pin = 11,  /* PA11 AF6 = SPI4_MISO (shared with USB D-) */
        .mosi_port = GPIO_PORT_A, .mosi_pin = 1,   /* PA1  AF5 = SPI4_MOSI */
        .sck_af = 6, .miso_af = 6, .mosi_af = 5,
    },
    [SPI_INSTANCE_5] = {
        .instance  = SPI_INSTANCE_5,
        .sck_port  = GPIO_PORT_B, .sck_pin  = 0,   /* PB0  AF6 = SPI5_SCK  */
        .miso_port = GPIO_PORT_A, .miso_pin = 12,  /* PA12 AF6 = SPI5_MISO (shared with USB D+) */
        .mosi_port = GPIO_PORT_A, .mosi_pin = 10,  /* PA10 AF6 = SPI5_MOSI */
        .sck_af = 6, .miso_af = 6, .mosi_af = 6,
    },
};

/* Static buffers for SPI transfer */
static uint8_t tx_buf[SPI_PERF_MAX_BUF_SIZE];
static uint8_t rx_buf[SPI_PERF_MAX_BUF_SIZE];

/**
 * @brief Fill TX buffer with a known pattern and clear RX buffer
 */
static void spi_perf_fill_patterns(uint16_t size) {
    for (uint16_t i = 0; i < size; i++) {
        tx_buf[i] = (uint8_t)((i+1) & 0xFF);
        rx_buf[i] = 0;
    }
}

/**
 * @brief Run a timed SPI transfer using the DWT cycle counter
 * @return Elapsed DWT cycles
 */
static uint32_t spi_perf_timed_transfer(spi_handle_t *handle, uint16_t size,
                                        uint8_t use_dma) {
    /* Enable DWT cycle counter */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    if (use_dma) {
        spi_transfer_dma_blocking(handle, tx_buf, rx_buf, size);
    } else {
        spi_transfer(handle, tx_buf, rx_buf, size);
    }

    return DWT->CYCCNT;
}

/**
 * @brief Print a byte buffer, truncating with "..." if longer than 8 entries
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

/**
 * @brief Return the peripheral bus clock for an SPI instance
 */
static uint32_t spi_perf_bus_clock(spi_instance_t inst) {
    switch (inst) {
        case SPI_INSTANCE_2:
        case SPI_INSTANCE_3:
            return SPI_PERF_APB1_CLOCK_HZ;
        default:
            return SPI_PERF_APB2_CLOCK_HZ;
    }
}

int spi_perf_run(spi_instance_t instance, uint16_t prescaler,
                 uint16_t buffer_size, uint8_t use_dma) {
    int br = spi_prescaler_to_br(prescaler);
    if (br < 0) return -1;
    if (instance >= SPI_INSTANCE_COUNT) return -1;

    uint32_t bus_clock = spi_perf_bus_clock(instance);
    uint32_t spi_freq_hz  = bus_clock / prescaler;
    uint32_t spi_freq_khz = spi_freq_hz / 1000;

    printf("--- SPI%u Master TX Test (%s) ---\n",
           (unsigned)(instance + 1), use_dma ? "DMA" : "polled");
    if (spi_freq_khz >= 1000) {
        printf("  Clock:  %lu MHz (prescaler %u)\n", spi_freq_khz / 1000, prescaler);
    } else {
        printf("  Clock:  %lu kHz (prescaler %u)\n", spi_freq_khz, prescaler);
    }
    printf("  Bytes:  %u\n", buffer_size);
    printf("  Peak Tput:   %lu KB/s\n", spi_freq_hz / 8000);

    /* Flush header output before running transfer */
    printf_dma_flush();

    /* Build SPI configuration from defaults + runtime parameters */
    spi_config_t cfg = spi_perf_pin_defaults[instance];
    cfg.prescaler_br = (uint8_t)br;
    cfg.cpol = 0;
    cfg.cpha = 0;

    /* Initialize SPI via generic driver */
    spi_handle_t spi;
    if (spi_init(&spi, &cfg) != 0) {
        printf("  ERROR: spi_init failed\n");
        return -1;
    }

    /* Fill test patterns */
    spi_perf_fill_patterns(buffer_size);

    /* Run timed transfer */
    uint32_t cycles = spi_perf_timed_transfer(&spi, buffer_size, use_dma);

    /* Compute timing */
    uint32_t clock_mhz = bus_clock / 1000000;
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
    spi_deinit(&spi);

    return 0;
}

#endif /* SPI_PERF_HOST_TEST */
