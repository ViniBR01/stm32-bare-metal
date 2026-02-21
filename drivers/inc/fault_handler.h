#ifndef FAULT_HANDLER_H_
#define FAULT_HANDLER_H_

/**
 * @file fault_handler.h
 * @brief HardFault diagnostic handler for Cortex-M4
 *
 * Provides a HardFault_Handler that extracts the stacked exception frame,
 * prints a register dump and fault status registers over blocking UART,
 * and blinks LED2 as a visual fault indicator.
 *
 * The HardFault_Handler symbol defined in fault_handler.c overrides the
 * weak alias in the startup file at link time -- no startup changes needed.
 *
 * Usage:
 * @code
 * #include "fault_handler.h"
 *
 * int main(void) {
 *     // Optional: enable DIV_0 trapping, individual fault handlers, etc.
 *     fault_handler_init();
 *
 *     // ... application code ...
 * }
 * @endcode
 */

/**
 * @brief Enable additional fault configuration (optional)
 *
 * Configures the SCB to:
 * - Trap divide-by-zero (DIVBYZERO in SCB->CCR)
 * - Enable MemManage, BusFault, and UsageFault handlers so they
 *   don't silently escalate to HardFault (SCB->SHCSR)
 *
 * Call this early in main() if you want more granular fault reporting.
 * If not called, all faults still land in HardFault_Handler and produce
 * a full diagnostic dump.
 */
void fault_handler_init(void);

#endif /* FAULT_HANDLER_H_ */
