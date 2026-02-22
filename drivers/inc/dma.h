#ifndef DMA_H
#define DMA_H

#include <stdint.h>

/**
 * @brief DMA stream identifiers
 *
 * Uniquely addresses each of the 16 DMA streams across DMA1 and DMA2.
 * Naming: DMA_STREAM_<controller>_<stream_number>
 */
typedef enum {
    DMA_STREAM_1_0 = 0,
    DMA_STREAM_1_1,
    DMA_STREAM_1_2,
    DMA_STREAM_1_3,
    DMA_STREAM_1_4,
    DMA_STREAM_1_5,
    DMA_STREAM_1_6,
    DMA_STREAM_1_7,
    DMA_STREAM_2_0,
    DMA_STREAM_2_1,
    DMA_STREAM_2_2,
    DMA_STREAM_2_3,
    DMA_STREAM_2_4,
    DMA_STREAM_2_5,
    DMA_STREAM_2_6,
    DMA_STREAM_2_7,
    DMA_STREAM_COUNT
} dma_stream_id_t;

/**
 * @brief DMA transfer direction
 *
 * Maps directly to the DIR[1:0] bits in the DMA_SxCR register.
 */
typedef enum {
    DMA_DIR_PERIPH_TO_MEM = 0,  /**< Peripheral to memory (P2M) */
    DMA_DIR_MEM_TO_PERIPH = 1,  /**< Memory to peripheral (M2P) */
    DMA_DIR_MEM_TO_MEM    = 2,  /**< Memory to memory (M2M)     */
} dma_direction_t;

/**
 * @brief DMA stream priority level
 *
 * Maps directly to the PL[1:0] bits in the DMA_SxCR register.
 */
typedef enum {
    DMA_PRIO_LOW       = 0,
    DMA_PRIO_MEDIUM    = 1,
    DMA_PRIO_HIGH      = 2,
    DMA_PRIO_VERY_HIGH = 3,
} dma_priority_t;

/**
 * @brief DMA callback function type
 *
 * @param stream  The stream that triggered the callback
 * @param ctx     Opaque user context pointer passed during configuration
 */
typedef void (*dma_callback_t)(dma_stream_id_t stream, void *ctx);

/**
 * @brief DMA stream configuration
 *
 * Passed to dma_stream_init() to allocate and configure a DMA stream.
 */
typedef struct {
    dma_stream_id_t   stream;         /**< Which stream to configure           */
    uint8_t           channel;        /**< Channel selection 0-7 (CHSEL bits)  */
    dma_direction_t   direction;      /**< Transfer direction                  */
    uint32_t          periph_addr;    /**< Peripheral register address (PAR)   */
    uint8_t           mem_inc;        /**< 1 = memory address increment (MINC) */
    uint8_t           periph_inc;     /**< 1 = peripheral address increment    */
    uint8_t           circular;       /**< 1 = circular mode (CIRC)            */
    dma_priority_t    priority;       /**< Stream priority level               */
    dma_callback_t    tc_callback;    /**< Transfer-complete callback (or NULL) */
    dma_callback_t    error_callback; /**< Error callback: TE/DME/FE (or NULL) */
    void             *cb_ctx;         /**< Opaque context passed to callbacks   */
    uint8_t           nvic_priority;  /**< NVIC interrupt priority (0 = highest)*/
} dma_stream_config_t;

/**
 * @brief Allocate and configure a DMA stream
 *
 * Enables the DMA controller clock, checks that the stream is not already
 * allocated, programs the CR register (channel, direction, MINC, CIRC,
 * priority, error interrupts, TC interrupt), and sets the peripheral address.
 *
 * @param cfg  Pointer to the stream configuration
 * @return 0 on success, -1 if the stream is invalid or already allocated
 */
int dma_stream_init(const dma_stream_config_t *cfg);

/**
 * @brief Start a DMA transfer on a previously initialized stream
 *
 * Clears pending interrupt flags, sets the memory address and data count,
 * then enables the stream.
 *
 * @param id        Stream identifier (must have been initialized)
 * @param mem_addr  Memory address (M0AR)
 * @param count     Number of data items to transfer (NDTR, 1-65535)
 * @return 0 on success, -1 if the stream is not allocated
 */
int dma_stream_start(dma_stream_id_t id, uint32_t mem_addr, uint16_t count);

/**
 * @brief Stop (disable) a DMA stream
 *
 * Disables the stream and waits for EN to clear. Does NOT release the
 * allocation -- the stream can be re-started with dma_stream_start().
 *
 * @param id  Stream identifier
 */
void dma_stream_stop(dma_stream_id_t id);

/**
 * @brief Release a DMA stream allocation
 *
 * Stops the stream (if running) and marks it as unallocated so another
 * driver may claim it.
 *
 * @param id  Stream identifier
 */
void dma_stream_release(dma_stream_id_t id);

/**
 * @brief Update the memory-increment (MINC) setting on an allocated stream
 *
 * Writes the new MINC value into the cached CR and the hardware register.
 * The stream must be stopped (EN = 0) before calling this.
 *
 * @param id      Stream identifier (must have been initialized)
 * @param enable  1 = enable MINC, 0 = disable MINC
 */
void dma_stream_set_mem_inc(dma_stream_id_t id, uint8_t enable);

/**
 * @brief Combined reconfigure-and-start for an already-stopped stream
 *
 * Updates the MINC bit, clears interrupt flags, sets the memory address and
 * transfer count, and enables the stream -- all in a single call with one
 * table lookup.  The stream MUST already be stopped (EN = 0); this function
 * does NOT poll for EN to clear, making it faster than a separate
 * dma_stream_set_mem_inc() + dma_stream_start() pair.
 *
 * @param id        Stream identifier (must have been initialized)
 * @param mem_addr  Memory address (M0AR)
 * @param count     Number of data items (NDTR, 1-65535)
 * @param mem_inc   1 = enable MINC, 0 = disable MINC
 * @return 0 on success, -1 if the stream is not allocated
 */
int dma_stream_start_config(dma_stream_id_t id, uint32_t mem_addr,
                            uint16_t count, uint8_t mem_inc);

/**
 * @brief Check whether a DMA stream is currently transferring
 *
 * @param id  Stream identifier
 * @return 1 if the stream EN bit is set, 0 otherwise
 */
int dma_stream_busy(dma_stream_id_t id);

/**
 * @brief Get the current NDTR (remaining items) for a stream
 *
 * Useful for circular-mode reception where the peripheral driver needs
 * to compute how many bytes have been received.
 *
 * @param id  Stream identifier
 * @return Current NDTR value, or 0 if the stream id is invalid
 */
uint16_t dma_stream_get_ndtr(dma_stream_id_t id);

#endif /* DMA_H */
