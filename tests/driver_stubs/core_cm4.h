/*
 * Stub core_cm4.h for driver host unit tests.
 *
 * Replaces the real CMSIS Cortex-M4 core header via include path ordering.
 * tests/driver_stubs/ must appear before chip_headers/CMSIS/Include/ in the
 * test Makefile -I flags so that #include "core_cm4.h" (inside stm32f411xe.h)
 * resolves here instead.
 *
 * Provides:
 *  - Volatile qualifier macros (__IO, __I, __O, __IM, __OM, __IOM)
 *  - Core peripheral structs: NVIC_Type, SCB_Type, SysTick_Type, DWT_Type,
 *    CoreDebug_Type
 *  - Peripheral instance pointers pointing at fake in-SRAM structs
 *  - NVIC control inline functions operating on fake_NVIC
 *  - Cortex-M intrinsic stubs (__get_PRIMASK, __disable_irq, __enable_irq)
 *
 * Note: IRQn_Type is defined in stm32f411xe.h BEFORE this file is included,
 * so NVIC inline functions can reference it directly.
 */

#ifndef STUB_CORE_CM4_H
#define STUB_CORE_CM4_H

#include <stdint.h>

/*
 * IRQn_Type is defined by stm32f411xe.h (as a typedef enum) BEFORE that
 * header includes this file. This is identical to how the real CMSIS
 * core_cm4.h works. The fallback below is only for static analysis tools
 * that inspect this file in isolation.
 */
#ifndef __STM32F411xE_H
typedef int32_t IRQn_Type;
#endif

/* ---- Volatile qualifier macros (used by stm32f411xe.h TypeDef structs) -- */

#define __I    volatile const
#define __O    volatile
#define __IO   volatile
#define __IM   volatile const
#define __OM   volatile
#define __IOM  volatile

/* ---- Inline function macros --------------------------------------------- */

#define __STATIC_INLINE       static inline
#define __STATIC_FORCEINLINE  static inline __attribute__((always_inline))
#define __WEAK                __attribute__((weak))

/* ---- NVIC_Type ----------------------------------------------------------- */

typedef struct {
    __IOM uint32_t ISER[8U];
          uint32_t RESERVED0[24U];
    __IOM uint32_t ICER[8U];
          uint32_t RESERVED1[24U];
    __IOM uint32_t ISPR[8U];
          uint32_t RESERVED2[24U];
    __IOM uint32_t ICPR[8U];
          uint32_t RESERVED3[24U];
    __IOM uint32_t IABR[8U];
          uint32_t RESERVED4[56U];
    __IOM uint8_t  IP[240U];
          uint32_t RESERVED5[644U];
    __OM  uint32_t STIR;
} NVIC_Type;

/* ---- SCB_Type ------------------------------------------------------------ */

typedef struct {
    __IM  uint32_t CPUID;
    __IOM uint32_t ICSR;
    __IOM uint32_t VTOR;
    __IOM uint32_t AIRCR;
    __IOM uint32_t SCR;
    __IOM uint32_t CCR;
    __IOM uint8_t  SHP[12U];
    __IOM uint32_t SHCSR;
    __IOM uint32_t CFSR;
    __IOM uint32_t HFSR;
    __IOM uint32_t DFSR;
    __IOM uint32_t MMFAR;
    __IOM uint32_t BFAR;
    __IOM uint32_t AFSR;
    __IM  uint32_t PFR[2U];
    __IM  uint32_t DFR;
    __IM  uint32_t ADR;
    __IM  uint32_t MMFR[4U];
    __IM  uint32_t ISAR[5U];
          uint32_t RESERVED0[5U];
    __IOM uint32_t CPACR;
} SCB_Type;

/* ---- SysTick_Type -------------------------------------------------------- */

typedef struct {
    __IOM uint32_t CTRL;
    __IOM uint32_t LOAD;
    __IOM uint32_t VAL;
    __IM  uint32_t CALIB;
} SysTick_Type;

/* ---- DWT_Type (used by spi_perf.c for cycle counting) ------------------- */

typedef struct {
    __IOM uint32_t CTRL;
    __IOM uint32_t CYCCNT;
    __IOM uint32_t CPICNT;
    __IOM uint32_t EXCCNT;
    __IOM uint32_t SLEEPCNT;
    __IOM uint32_t LSUCNT;
    __IOM uint32_t FOLDCNT;
    __IM  uint32_t PCSR;
} DWT_Type;

/* ---- CoreDebug_Type (used by spi_perf.c) -------------------------------- */

typedef struct {
    __IOM uint32_t DHCSR;
    __OM  uint32_t DCRSR;
    __IOM uint32_t DCRDR;
    __IOM uint32_t DEMCR;
} CoreDebug_Type;

/* ---- Fake core peripheral instances (defined in test_periph.c) ---------- */

extern NVIC_Type        fake_NVIC;
extern SCB_Type         fake_SCB;
extern SysTick_Type     fake_SysTick;
extern DWT_Type         fake_DWT;
extern CoreDebug_Type   fake_CoreDebug;

/* ---- Instance macros (override real CMSIS hardware addresses) ----------- */

#define NVIC        (&fake_NVIC)
#define SCB         (&fake_SCB)
#define SysTick     (&fake_SysTick)
#define DWT         (&fake_DWT)
#define CoreDebug   (&fake_CoreDebug)

/* ---- NVIC priority bits ------------------------------------------------- */

#define __NVIC_PRIO_BITS    4U

/* ---- NVIC inline functions (operate on fake_NVIC) ----------------------- */

static inline void NVIC_EnableIRQ(IRQn_Type IRQn)
{
    if ((int32_t)IRQn >= 0)
        fake_NVIC.ISER[(uint32_t)IRQn >> 5U] |= (1U << ((uint32_t)IRQn & 0x1FU));
}

static inline void NVIC_DisableIRQ(IRQn_Type IRQn)
{
    if ((int32_t)IRQn >= 0)
        fake_NVIC.ICER[(uint32_t)IRQn >> 5U] = (1U << ((uint32_t)IRQn & 0x1FU));
}

static inline void NVIC_SetPriority(IRQn_Type IRQn, uint32_t priority)
{
    if ((int32_t)IRQn >= 0)
        fake_NVIC.IP[(uint32_t)IRQn] =
            (uint8_t)((priority << (8U - __NVIC_PRIO_BITS)) & 0xFFU);
}

static inline uint32_t NVIC_GetPriority(IRQn_Type IRQn)
{
    if ((int32_t)IRQn >= 0)
        return (uint32_t)(fake_NVIC.IP[(uint32_t)IRQn] >> (8U - __NVIC_PRIO_BITS));
    return 0U;
}

/* ---- Cortex-M intrinsic stubs ------------------------------------------- */

static inline uint32_t __get_PRIMASK(void) { return 0U; }
static inline void     __disable_irq(void) {}
static inline void     __enable_irq(void)  {}
static inline void     __DSB(void)         {}
static inline void     __ISB(void)         {}
static inline void     __NOP(void)         {}
static inline void     __WFI(void)         {}

/* ---- DWT / CoreDebug bit constants (used by spi_perf.c) ----------------- */

#define DWT_CTRL_CYCCNTENA_Msk        (1UL << 0)
#define CoreDebug_DEMCR_TRCENA_Msk    (1UL << 24)

/* ---- SCB bit constants (used by fault_handler.c, sleep_mode.c) ---------- */

#define SCB_SCR_SLEEPDEEP_Msk         (1UL << 2)
#define SCB_SCR_SLEEPONEXIT_Msk       (1UL << 1)

#endif /* STUB_CORE_CM4_H */
