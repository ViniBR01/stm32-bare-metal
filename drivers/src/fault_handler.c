/**
 * @file fault_handler.c
 * @brief HardFault diagnostic handler for Cortex-M4 (STM32F411)
 *
 * Provides a strong HardFault_Handler that overrides the weak alias in the
 * startup file.  On a fault the handler:
 *   1. Determines which stack (MSP / PSP) was active via EXC_RETURN.
 *   2. Extracts the hardware-stacked exception frame (R0-R3, R12, LR, PC, xPSR).
 *   3. Reads the fault-status registers (CFSR, HFSR, MMFAR, BFAR).
 *   4. Prints everything over UART using blocking uart_write() (DMA-safe).
 *   5. Blinks LED2 in an SOS-like pattern and halts in an infinite loop.
 */

#include <stdint.h>
#include "stm32f4xx.h"
#include "printf.h"
#include "uart.h"
#include "led2.h"
#include "fault_handler.h"

/* ------------------------------------------------------------------ */
/*  Private helpers                                                    */
/* ------------------------------------------------------------------ */

/**
 * @brief Write a null-terminated string over UART using blocking writes.
 *
 * Intentionally avoids DMA -- during a fault the DMA controller may be
 * in an unknown state.
 */
static void fault_puts(const char *s)
{
    while (*s) {
        uart_write(*s++);
    }
}

/**
 * @brief Crude busy-wait delay (~1 ms per count at 100 MHz).
 *
 * SysTick may not be available during a fault so we use a spin loop.
 * The exact timing is approximate and depends on clock speed / compiler
 * optimisation level, but it is good enough for a visual blink pattern.
 */
static void fault_delay_ms(volatile uint32_t ms)
{
    /* Each inner iteration is roughly 4 cycles on Cortex-M4.
     * At 100 MHz that gives ~25 000 iterations per millisecond. */
    while (ms--) {
        volatile uint32_t count = 25000U;
        while (count--) {
            __asm volatile ("nop");
        }
    }
}

/**
 * @brief Blink LED2 in a recognisable SOS pattern and never return.
 *
 * Pattern: ... --- ... (3 short, 3 long, 3 short) repeated forever.
 */
static void fault_blink_forever(void)
{
    /* Best-effort LED init -- GPIO clock may already be enabled */
    led2_init();
    led2_off();

    for (;;) {
        /* S: three short blinks */
        for (int i = 0; i < 3; i++) {
            led2_on();
            fault_delay_ms(150);
            led2_off();
            fault_delay_ms(150);
        }

        fault_delay_ms(300);

        /* O: three long blinks */
        for (int i = 0; i < 3; i++) {
            led2_on();
            fault_delay_ms(450);
            led2_off();
            fault_delay_ms(150);
        }

        fault_delay_ms(300);

        /* S: three short blinks */
        for (int i = 0; i < 3; i++) {
            led2_on();
            fault_delay_ms(150);
            led2_off();
            fault_delay_ms(150);
        }

        /* Inter-word gap */
        fault_delay_ms(1000);
    }
}

/* ------------------------------------------------------------------ */
/*  C fault-print function (called from the asm trampoline)           */
/* ------------------------------------------------------------------ */

/**
 * @brief Print the stacked exception frame and fault-status registers.
 *
 * @param stack_frame  Pointer to the exception stack frame (MSP or PSP).
 *
 * The Cortex-M4 hardware pushes the following layout onto the stack:
 *   [0] R0   [1] R1   [2] R2   [3] R3
 *   [4] R12  [5] LR   [6] PC   [7] xPSR
 */
__attribute__((used))
void fault_handler_print(uint32_t *stack_frame)
{
    char buf[48];

    /* Stacked core registers */
    uint32_t r0   = stack_frame[0];
    uint32_t r1   = stack_frame[1];
    uint32_t r2   = stack_frame[2];
    uint32_t r3   = stack_frame[3];
    uint32_t r12  = stack_frame[4];
    uint32_t lr   = stack_frame[5];
    uint32_t pc   = stack_frame[6];
    uint32_t xpsr = stack_frame[7];

    /* Fault-status registers */
    uint32_t cfsr  = SCB->CFSR;
    uint32_t hfsr  = SCB->HFSR;
    uint32_t mmfar = SCB->MMFAR;
    uint32_t bfar  = SCB->BFAR;

    /* ---- Print header ---- */
    fault_puts("\n======== HARD FAULT ========\n");

    /* ---- Stacked registers ---- */
    snprintf(buf, sizeof(buf), "R0  = 0x%08lX\n", (unsigned long)r0);
    fault_puts(buf);

    snprintf(buf, sizeof(buf), "R1  = 0x%08lX\n", (unsigned long)r1);
    fault_puts(buf);

    snprintf(buf, sizeof(buf), "R2  = 0x%08lX\n", (unsigned long)r2);
    fault_puts(buf);

    snprintf(buf, sizeof(buf), "R3  = 0x%08lX\n", (unsigned long)r3);
    fault_puts(buf);

    snprintf(buf, sizeof(buf), "R12 = 0x%08lX\n", (unsigned long)r12);
    fault_puts(buf);

    snprintf(buf, sizeof(buf), "LR  = 0x%08lX\n", (unsigned long)lr);
    fault_puts(buf);

    snprintf(buf, sizeof(buf), "PC  = 0x%08lX  <-- faulting instruction\n", (unsigned long)pc);
    fault_puts(buf);

    snprintf(buf, sizeof(buf), "xPSR= 0x%08lX\n", (unsigned long)xpsr);
    fault_puts(buf);

    /* ---- Fault status registers ---- */
    fault_puts("---- Fault Status ----\n");

    snprintf(buf, sizeof(buf), "CFSR = 0x%08lX\n", (unsigned long)cfsr);
    fault_puts(buf);

    snprintf(buf, sizeof(buf), "HFSR = 0x%08lX\n", (unsigned long)hfsr);
    fault_puts(buf);

    snprintf(buf, sizeof(buf), "MMFAR= 0x%08lX\n", (unsigned long)mmfar);
    fault_puts(buf);

    snprintf(buf, sizeof(buf), "BFAR = 0x%08lX\n", (unsigned long)bfar);
    fault_puts(buf);

    fault_puts("========================\n");

    /* ---- Visual indicator + halt ---- */
    fault_blink_forever();
}

/* ------------------------------------------------------------------ */
/*  Assembly trampoline (naked -- no compiler-generated prologue)     */
/* ------------------------------------------------------------------ */

/**
 * @brief Common naked trampoline body.
 *
 * Every fault handler needs the same sequence: test EXC_RETURN bit 2 to
 * pick MSP vs PSP, then branch to the C print function.  We wrap it in
 * a macro so all four handlers share the identical instruction stream.
 *
 * EXC_RETURN bit 2:
 *   0 -> MSP was used (handler / main stack)
 *   1 -> PSP was used (thread / process stack)
 */
#define FAULT_TRAMPOLINE()                           \
    __asm volatile(                                  \
        "tst   lr, #4          \n"                   \
        "ite   eq              \n"                   \
        "mrseq r0, msp         \n"                   \
        "mrsne r0, psp         \n"                   \
        "b     fault_handler_print \n"               \
    )

__attribute__((naked))
void HardFault_Handler(void)  { FAULT_TRAMPOLINE(); }

__attribute__((naked))
void MemManage_Handler(void)  { FAULT_TRAMPOLINE(); }

__attribute__((naked))
void BusFault_Handler(void)   { FAULT_TRAMPOLINE(); }

__attribute__((naked))
void UsageFault_Handler(void) { FAULT_TRAMPOLINE(); }

/* ------------------------------------------------------------------ */
/*  Optional initialisation helper                                     */
/* ------------------------------------------------------------------ */

void fault_handler_init(void)
{
    /* Enable DIV_0 and unaligned-access trapping */
    SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk;

    /* Enable MemManage, BusFault and UsageFault handlers so they fire
     * independently instead of all escalating to HardFault.            */
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk
                |  SCB_SHCSR_BUSFAULTENA_Msk
                |  SCB_SHCSR_USGFAULTENA_Msk;
}
