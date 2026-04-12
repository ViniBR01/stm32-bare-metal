# Driver: SPI

**Files:** `drivers/inc/spi.h`, `drivers/src/spi.c`

## Purpose

SPI master driver supporting all 5 SPI instances on the STM32F411RE. Used in the CLI for hardware throughput testing and the shift register example.

## Features

- All 5 SPI instances (SPI1–SPI5)
- Fully configurable: SCK/MISO/MOSI pins, alternate functions, prescaler, CPOL, CPHA
- **Polled transfer** (`spi_transfer`) — blocking, full-duplex
- **DMA transfer** (`spi_transfer_dma`) — non-blocking; `spi_transfer_dma_blocking` spins until done
- SPI1/4/5 on APB2 (100 MHz bus); SPI2/3 on APB1 (50 MHz bus)

## API

```c
int  spi_init(spi_handle_t *handle, const spi_config_t *config);  // 0=ok, -1=error
void spi_deinit(spi_handle_t *handle);
void spi_enable(spi_handle_t *handle);
void spi_disable(spi_handle_t *handle);

// Polled (blocking)
int spi_transfer(spi_handle_t *handle,
                 const uint8_t *tx, uint8_t *rx, uint16_t len);

// DMA (non-blocking; poll handle->dma_busy to know when done)
int spi_transfer_dma(spi_handle_t *handle,
                     const uint8_t *tx, uint8_t *rx, uint16_t len);

// DMA (blocking; waits for dma_busy to clear)
int spi_transfer_dma_blocking(spi_handle_t *handle,
                              const uint8_t *tx, uint8_t *rx, uint16_t len);

// Helper
int spi_prescaler_to_br(uint16_t prescaler);  // 2/4/.../256 → BR[2:0] value 0-7
```

## Configuration struct

```c
typedef struct {
    spi_instance_t  instance;     // SPI_INSTANCE_1 .. SPI_INSTANCE_5
    gpio_port_t     sck_port;
    uint8_t         sck_pin;
    gpio_port_t     miso_port;
    uint8_t         miso_pin;
    gpio_port_t     mosi_port;
    uint8_t         mosi_pin;
    uint8_t         sck_af;       // GPIO alternate function number
    uint8_t         miso_af;
    uint8_t         mosi_af;
    uint8_t         prescaler_br; // BR[2:0] field (use spi_prescaler_to_br())
    uint8_t         cpol;         // 0 or 1
    uint8_t         cpha;         // 0 or 1
} spi_config_t;
```

## Handle

```c
typedef struct {
    void            *regs;        // SPI_TypeDef* — set by spi_init
    spi_config_t     config;
    volatile uint8_t dma_busy;   // poll this for non-blocking DMA completion
} spi_handle_t;
```

## Transfer semantics

- `tx == NULL` → sends 0xFF for each byte (read-only transfer)
- `rx == NULL` → received data discarded (write-only transfer)
- `spi_init` does NOT enable SPE; call `spi_enable()` or let `spi_transfer()` manage it
- `spi_transfer()` enables SPE before the transfer and disables it after

## Performance

SPI performance testing is built into the CLI via the `spi_perf_test` command, which uses the DWT cycle counter to measure throughput in KB/s. See `drivers/inc/spi_perf.h`.
