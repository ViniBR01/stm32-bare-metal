#ifndef CRITICAL_SECTION_H
#define CRITICAL_SECTION_H

#include "stm32f4xx.h"
#include "irq_priorities.h"

/*
 * Nestable priority-based critical section using BASEPRI.
 *
 * Masks all interrupts at IRQ_PRIO_UART (2) and below while leaving
 * DMA ISRs (priority 0, 1) free to fire -- avoiding buffer overruns
 * when protecting shared data accessed from both task and ISR contexts.
 *
 * Usage:
 *   uint32_t saved = critical_section_enter();
 *   // protected code
 *   critical_section_exit(saved);
 */
static inline uint32_t critical_section_enter(void)
{
    uint32_t prev = __get_BASEPRI();
    __set_BASEPRI(IRQ_BASEPRI_MASK << (8U - __NVIC_PRIO_BITS));
    return prev;
}

static inline void critical_section_exit(uint32_t saved)
{
    __set_BASEPRI(saved);
}

#endif /* CRITICAL_SECTION_H */
