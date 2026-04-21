#ifndef IRQ_PRIORITIES_H
#define IRQ_PRIORITIES_H

#include "stm32f4xx.h"   /* __NVIC_PRIO_BITS = 4 on STM32F411 */

/*
 * Priority grouping: PRIGROUP=0 -> all 4 bits are preemption priority,
 * 0 sub-priority bits.  Levels 0 (highest) ... 15 (lowest).
 * Called once in Reset_Handler before any peripheral init.
 */
#define NVIC_PRIORITY_GROUP     0U

/*
 * Peripheral priority levels (lower value = fires first).
 *
 * Hierarchy rationale:
 *   DMA streams must be able to preempt the UART ISR so that a DMA
 *   transfer-complete interrupt can fire even while the UART error ISR
 *   is already running -- preventing buffer overruns under sustained load.
 *   EXTI and timer ISRs are application-level and can tolerate more latency.
 *
 *   0  IRQ_PRIO_DMA_HIGH -- DMA TX streams (UART TX, SPI)
 *   1  IRQ_PRIO_DMA_LOW  -- DMA RX streams (UART RX, SPI)
 *   2  IRQ_PRIO_UART     -- USART ISR (error flags / idle / RXNE)
 *   3  IRQ_PRIO_EXTI     -- GPIO edge interrupts
 *   3  IRQ_PRIO_TIMER    -- General-purpose timer update ISR
 */
#define IRQ_PRIO_DMA_HIGH    0U
#define IRQ_PRIO_DMA_LOW     1U
#define IRQ_PRIO_UART        2U
#define IRQ_PRIO_EXTI        3U
#define IRQ_PRIO_TIMER       3U

/*
 * BASEPRI threshold for critical sections.
 * Masking at IRQ_PRIO_UART (2) leaves DMA ISRs (0,1) able to fire
 * during critical sections protecting shared UART/SPI buffers.
 */
#define IRQ_BASEPRI_MASK     IRQ_PRIO_UART

#endif /* IRQ_PRIORITIES_H */
