# Driver: DMA

**Files:** `drivers/inc/dma.h`, `drivers/src/dma.c`

## Purpose

Generic DMA stream driver for the STM32F411RE. Provides stream allocation, configuration, start/stop, and transfer-complete callbacks. Used internally by the UART and SPI drivers.

## Design

- **Ownership model:** A stream must be explicitly `init`-ed before use and `release`-ed when done. An already-allocated stream returns -1 on `dma_stream_init()`.
- **Callback-based:** Transfer-complete and error callbacks are registered at init time, called from the DMA ISR.

## API

```c
int  dma_stream_init(const dma_stream_config_t *cfg);          // allocate + configure
int  dma_stream_start(dma_stream_id_t id, uint32_t mem_addr, uint16_t count);
void dma_stream_stop(dma_stream_id_t id);
void dma_stream_release(dma_stream_id_t id);                   // stop + deallocate

// In-flight operations (stream must be stopped first)
void dma_stream_set_mem_inc(dma_stream_id_t id, uint8_t enable);

// Fast reconfigure-and-start (avoids separate set_mem_inc + start calls)
int  dma_stream_start_config(dma_stream_id_t id, uint32_t mem_addr,
                             uint16_t count, uint8_t mem_inc);

// Status
int      dma_stream_busy(dma_stream_id_t id);        // 1 if EN bit set
uint16_t dma_stream_get_ndtr(dma_stream_id_t id);    // remaining items (useful for circular RX)
```

## Stream identifiers

16 streams total across two controllers:

```
DMA_STREAM_1_0 .. DMA_STREAM_1_7   (DMA1, streams 0-7)
DMA_STREAM_2_0 .. DMA_STREAM_2_7   (DMA2, streams 0-7)
```

## Configuration struct

```c
typedef struct {
    dma_stream_id_t   stream;
    uint8_t           channel;        // 0-7 (CHSEL bits)
    dma_direction_t   direction;      // PERIPH_TO_MEM, MEM_TO_PERIPH, MEM_TO_MEM
    uint32_t          periph_addr;    // PAR register value
    uint8_t           mem_inc;        // 1 = increment memory address (MINC)
    uint8_t           periph_inc;     // 1 = increment peripheral address
    uint8_t           circular;       // 1 = circular mode (CIRC)
    dma_priority_t    priority;       // LOW / MEDIUM / HIGH / VERY_HIGH
    dma_callback_t    tc_callback;    // transfer-complete (or NULL)
    dma_callback_t    error_callback; // TE/DME/FE errors (or NULL)
    void             *cb_ctx;         // opaque context passed to callbacks
    uint8_t           nvic_priority;  // 0 = highest
} dma_stream_config_t;
```

## Stream assignments (current usage)

| Driver | Direction | Stream | Channel |
|---|---|---|---|
| UART TX | M→P | DMA1 Stream 6 | Ch 4 |
| UART RX | P→M | DMA1 Stream 5 | Ch 4 |
| SPI TX | M→P | _(configured per instance)_ | |
| SPI RX | P→M | _(configured per instance)_ | |
