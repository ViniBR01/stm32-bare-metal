#ifndef SPI_PERF_H
#define SPI_PERF_H

#include <stdint.h>

/**
 * @brief Parsed SPI performance test arguments
 */
typedef struct {
    uint16_t prescaler;    /**< Baud rate prescaler (2, 4, 8, ..., 256) */
    uint16_t buffer_size;  /**< Transfer buffer size in bytes (1-256) */
    int error;             /**< 0 = ok, non-zero = parse/validation error */
} spi_perf_args_t;

/**
 * @brief Default prescaler value when not specified
 */
#define SPI_PERF_DEFAULT_PRESCALER  4

/**
 * @brief Default buffer size when not specified (3 bytes for easy logic analyzer debugging)
 */
#define SPI_PERF_DEFAULT_BUF_SIZE   3

/**
 * @brief Maximum buffer size supported
 */
#define SPI_PERF_MAX_BUF_SIZE       256

/**
 * @brief APB1 clock frequency (SPI2 clock source)
 */
#define SPI_PERF_APB1_CLOCK_HZ     16000000

/**
 * @brief Parse CLI arguments into SPI perf test config
 *
 * Parses optional prescaler and buffer_size from the argument string.
 * If args is empty, defaults are used (prescaler=4, buffer_size=3).
 *
 * @param args Argument string (may be empty, must not be NULL)
 * @return Parsed arguments with error flag
 */
spi_perf_args_t spi_perf_parse_args(const char* args);

/**
 * @brief Convert prescaler value to SPI CR1 BR[2:0] bits
 *
 * @param prescaler Prescaler value (must be power of 2, 2-256)
 * @return BR field value (0-7) on success, -1 if invalid
 */
int spi_prescaler_to_br(uint16_t prescaler);

/**
 * @brief Run the SPI transfer test
 *
 * Initializes SPI2 as master on PB13 (SCK), PB14 (MISO), PB15 (MOSI),
 * transmits a known pattern, and prints results. Uses a single SPI
 * interface for easy debugging with a logic analyzer.
 *
 * @param prescaler Baud rate prescaler (2, 4, 8, ..., 256)
 * @param buffer_size Number of bytes to transfer (1-256)
 * @return 0 on success, non-zero on error
 */
int spi_perf_run(uint16_t prescaler, uint16_t buffer_size);

#endif /* SPI_PERF_H */
