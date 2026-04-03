/*
 * Host-test stub for utils/inc/printf_dma.h
 *
 * The real implementation drives a DMA-backed UART transmitter which does
 * not exist on the host.  All functions are no-ops; cli.c calls only
 * printf_dma_flush() so that is the only one that matters for the tests.
 */
#pragma once

static inline void printf_dma_init(void)                  {}
static inline void printf_dma_process(void)               {}
static inline void printf_dma_tx_complete_callback(void)  {}
static inline void printf_dma_mark_pending(void)          {}
static inline void printf_dma_flush(void)                 {}
