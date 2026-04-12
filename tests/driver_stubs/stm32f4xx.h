/*
 * Stub stm32f4xx.h for driver host unit tests.
 *
 * Shadows the real chip header via include path ordering:
 *   tests/driver_stubs/ must come BEFORE chip_headers/ in -I flags.
 *
 * Strategy: include the REAL stm32f411xe.h device header to get all
 * TypeDef structs and bit-flag constants exactly as they appear in
 * production (accurate, zero maintenance). Then include test_periph.h
 * which #undef/#redefine all peripheral instance macros to point at
 * global fake structs in SRAM.
 *
 * Include chain:
 *   driver.c
 *     #include "stm32f4xx.h"          ← this file (from driver_stubs/)
 *       #include "stm32f411xe.h"       ← real device header (chip_headers/)
 *         #include "core_cm4.h"        ← our stub (from driver_stubs/)
 *         #include "system_stm32f4xx.h" ← real system header (chip_headers/)
 *       #include "test_periph.h"       ← fake instances + macro overrides
 */

#ifndef STUB_STM32F4XX_H
#define STUB_STM32F4XX_H

/* Select the STM32F411xE device variant — required by stm32f411xe.h */
#ifndef STM32F411xE
#define STM32F411xE
#endif

/*
 * Include the real device header. It will pull in:
 *  - our core_cm4.h stub (for NVIC/SysTick structs and intrinsics)
 *  - system_stm32f4xx.h (real, just declares SystemCoreClock and SystemInit)
 *  - All peripheral TypeDef structs (GPIO_TypeDef, RCC_TypeDef, etc.)
 *  - All bit-flag constants (RCC_AHB1ENR_GPIOAEN, SPI_CR1_MSTR, etc.)
 *  - Peripheral BASE macros and instance macros (GPIOA, RCC, SPI2, ...)
 */
#include "stm32f411xe.h"

/*
 * Override all peripheral instance macros to point at fake structs.
 * Must come AFTER stm32f411xe.h so the TypeDef types are already defined.
 */
#include "test_periph.h"

#endif /* STUB_STM32F4XX_H */
