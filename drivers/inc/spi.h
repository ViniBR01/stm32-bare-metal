#ifndef SPI_H
#define SPI_H

#include <stdint.h>
#include "gpio_handler.h"

/**
 * @brief SPI peripheral instance identifiers
 *
 * The STM32F411RE has five SPI peripherals.
 * SPI1, SPI4, SPI5 are on APB2; SPI2, SPI3 are on APB1.
 */
typedef enum {
    SPI_INSTANCE_1,
    SPI_INSTANCE_2,
    SPI_INSTANCE_3,
    SPI_INSTANCE_4,
    SPI_INSTANCE_5,
    SPI_INSTANCE_COUNT
} spi_instance_t;

/**
 * @brief SPI configuration parameters
 */
typedef struct {
    spi_instance_t  instance;       /**< Which SPI peripheral to use */
    gpio_port_t     sck_port;       /**< GPIO port for SCK pin */
    uint8_t         sck_pin;        /**< GPIO pin number for SCK (0-15) */
    gpio_port_t     miso_port;      /**< GPIO port for MISO pin */
    uint8_t         miso_pin;       /**< GPIO pin number for MISO (0-15) */
    gpio_port_t     mosi_port;      /**< GPIO port for MOSI pin */
    uint8_t         mosi_pin;       /**< GPIO pin number for MOSI (0-15) */
    uint8_t         sck_af;         /**< GPIO AF number for SCK pin */
    uint8_t         miso_af;        /**< GPIO AF number for MISO pin */
    uint8_t         mosi_af;        /**< GPIO AF number for MOSI pin */
    uint8_t         prescaler_br;   /**< Baud rate BR[2:0] value (0-7) */
    uint8_t         cpol;           /**< Clock polarity (0 or 1) */
    uint8_t         cpha;           /**< Clock phase (0 or 1) */
} spi_config_t;

/**
 * @brief SPI driver handle
 *
 * Holds runtime state for an initialized SPI peripheral.
 * Allocated by the caller, populated by spi_init().
 */
typedef struct {
    void           *regs;           /**< Pointer to SPI register block (SPI_TypeDef*) */
    spi_config_t    config;         /**< Stored configuration */
    volatile uint8_t dma_busy;      /**< Non-zero while a DMA transfer is in progress */
} spi_handle_t;

/**
 * @brief Initialize an SPI peripheral
 *
 * Enables the peripheral clock, configures GPIO pins in AF mode,
 * and sets up CR1 for master mode (SSM/SSI, BR, CPOL, CPHA, 8-bit, MSB first).
 * Does NOT enable SPE -- call spi_enable() when ready to transfer.
 *
 * @param handle  Caller-allocated handle to populate
 * @param config  Configuration parameters
 * @return 0 on success, -1 on invalid parameters
 */
int spi_init(spi_handle_t *handle, const spi_config_t *config);

/**
 * @brief Deinitialize an SPI peripheral
 *
 * Disables SPE and turns off the peripheral clock.
 *
 * @param handle  Initialized SPI handle
 */
void spi_deinit(spi_handle_t *handle);

/**
 * @brief Enable the SPI peripheral (set SPE)
 *
 * @param handle  Initialized SPI handle
 */
void spi_enable(spi_handle_t *handle);

/**
 * @brief Disable the SPI peripheral (clear SPE)
 *
 * @param handle  Initialized SPI handle
 */
void spi_disable(spi_handle_t *handle);

/**
 * @brief Perform a full-duplex SPI transfer (blocking, polled)
 *
 * Sends @p len bytes from @p tx while simultaneously receiving into @p rx.
 * - If @p tx is NULL, 0xFF is sent for each byte (useful for read-only transfers).
 * - If @p rx is NULL, received data is discarded (useful for write-only transfers).
 *
 * Enables SPE before the transfer and disables it after completion.
 *
 * @param handle  Initialized SPI handle
 * @param tx      Transmit buffer (may be NULL)
 * @param rx      Receive buffer (may be NULL)
 * @param len     Number of bytes to transfer
 * @return 0 on success, -1 on error
 */
int spi_transfer(spi_handle_t *handle, const uint8_t *tx, uint8_t *rx, uint16_t len);

/**
 * @brief Start a full-duplex SPI transfer using DMA (non-blocking)
 *
 * Configures two DMA streams (TX and RX), enables SPE, and returns
 * immediately.  The transfer completes in the background; poll
 * handle->dma_busy (cleared by the DMA RX-complete ISR) to know when
 * it finishes.
 *
 * - If @p tx is NULL, 0xFF is sent for each byte.
 * - If @p rx is NULL, received data is discarded.
 *
 * @param handle  Initialized SPI handle
 * @param tx      Transmit buffer (may be NULL)
 * @param rx      Receive buffer (may be NULL)
 * @param len     Number of bytes to transfer (1-65535)
 * @return 0 on success, -1 on error
 */
int spi_transfer_dma(spi_handle_t *handle, const uint8_t *tx, uint8_t *rx, uint16_t len);

/**
 * @brief Perform a full-duplex SPI transfer using DMA (blocking)
 *
 * Same as spi_transfer_dma() but spins until the transfer completes.
 *
 * @param handle  Initialized SPI handle
 * @param tx      Transmit buffer (may be NULL)
 * @param rx      Receive buffer (may be NULL)
 * @param len     Number of bytes to transfer (1-65535)
 * @return 0 on success, -1 on error
 */
int spi_transfer_dma_blocking(spi_handle_t *handle, const uint8_t *tx, uint8_t *rx, uint16_t len);

/**
 * @brief Convert a human-readable prescaler value to the BR[2:0] bit field
 *
 * @param prescaler  Prescaler divider (must be power of 2: 2, 4, 8, ..., 256)
 * @return BR field value (0-7) on success, -1 if prescaler is invalid
 */
int spi_prescaler_to_br(uint16_t prescaler);

#endif /* SPI_H */
