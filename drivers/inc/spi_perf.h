#ifndef SPI_PERF_H
#define SPI_PERF_H

#include <stdint.h>

/**
 * @brief Parsed SPI performance test arguments
 */
typedef struct {
    uint16_t prescaler;    /**< Baud rate prescaler (2, 4, 8, ..., 256) */
    uint16_t buffer_size;  /**< Transfer buffer size in bytes (1-16384) */
    int error;             /**< 0 = ok, non-zero = parse/validation error */
} spi_perf_args_t;

/**
 * @brief Default prescaler value when not specified
 */
#define SPI_PERF_DEFAULT_PRESCALER  4

/**
 * @brief Default buffer size when not specified
 */
#define SPI_PERF_DEFAULT_BUF_SIZE   10240

/**
 * @brief Maximum buffer size supported
 */
#define SPI_PERF_MAX_BUF_SIZE       16384

/**
 * @brief APB2 clock frequency (SPI1 master clock source)
 */
#define SPI_PERF_APB2_CLOCK_HZ     16000000

/**
 * @brief Parse CLI arguments into SPI perf test config
 *
 * Parses optional prescaler and buffer_size from the argument string.
 * If args is empty, defaults are used (prescaler=4, buffer_size=10240).
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
 * @brief Run the SPI performance test
 *
 * Initializes SPI1 (master) and SPI2 (slave), performs a full-duplex
 * transfer, measures throughput, verifies data integrity, prints results,
 * and deinitializes the peripherals.
 *
 * @param prescaler Baud rate prescaler (2, 4, 8, ..., 256)
 * @param buffer_size Number of bytes to transfer (1-16384)
 * @return 0 on success, non-zero on error
 */
int spi_perf_run(uint16_t prescaler, uint16_t buffer_size);

#endif /* SPI_PERF_H */
