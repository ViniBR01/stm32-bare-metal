#ifndef SPI_PERF_H
#define SPI_PERF_H

#include <stdint.h>
#include "spi.h"

/**
 * @brief Parsed SPI performance test arguments
 */
typedef struct {
    spi_instance_t instance;   /**< SPI peripheral to test (default: SPI2) */
    uint16_t prescaler;        /**< Baud rate prescaler (2, 4, 8, ..., 256) */
    uint16_t buffer_size;      /**< Transfer buffer size in bytes (1-256) */
    uint8_t  use_dma;          /**< Non-zero to use DMA transfer mode */
    int error;                 /**< 0 = ok, non-zero = parse/validation error */
} spi_perf_args_t;

/**
 * @brief Default SPI instance when not specified
 */
#define SPI_PERF_DEFAULT_INSTANCE   SPI_INSTANCE_2

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
 * @brief Parse CLI arguments into SPI perf test config
 *
 * Parses optional spi_num, prescaler, and buffer_size from the argument string.
 * If args is empty, defaults are used (spi=2, prescaler=4, buffer_size=3).
 *
 * @param args Argument string (may be empty, must not be NULL)
 * @return Parsed arguments with error flag
 */
spi_perf_args_t spi_perf_parse_args(const char* args);

/**
 * @brief Run the SPI transfer performance test
 *
 * Initializes the selected SPI peripheral as master, transmits a known
 * pattern, measures transfer time with the DWT cycle counter, and prints
 * throughput results. Uses the generic SPI driver internally.
 *
 * @param instance    SPI peripheral to test
 * @param prescaler   Baud rate prescaler (2, 4, 8, ..., 256)
 * @param buffer_size Number of bytes to transfer (1-256)
 * @param use_dma     Non-zero to use DMA transfer mode
 * @return 0 on success, non-zero on error
 */
int spi_perf_run(spi_instance_t instance, uint16_t prescaler,
                 uint16_t buffer_size, uint8_t use_dma);

#endif /* SPI_PERF_H */
