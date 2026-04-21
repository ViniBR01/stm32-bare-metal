/*
 * test_periph.c — Definitions of all fake STM32 peripheral instances.
 *
 * Compiled into every driver test suite executable alongside the driver
 * source under test and the Unity test file.
 *
 * All instances are zero-initialised at program start (C static storage).
 * Call test_periph_reset() in setUp() before each Unity test case to
 * ensure a clean, predictable state between tests.
 */

#include "stm32f4xx.h"  /* TypeDef types via our stub */
#include "test_periph.h"
#include <string.h>

/* ---- STM32 peripheral fake instances ------------------------------------ */

GPIO_TypeDef    fake_GPIOA;
GPIO_TypeDef    fake_GPIOB;
GPIO_TypeDef    fake_GPIOC;
GPIO_TypeDef    fake_GPIOD;
GPIO_TypeDef    fake_GPIOE;
GPIO_TypeDef    fake_GPIOH;

RCC_TypeDef     fake_RCC;

USART_TypeDef   fake_USART2;

SPI_TypeDef     fake_SPI1;
SPI_TypeDef     fake_SPI2;
SPI_TypeDef     fake_SPI3;
SPI_TypeDef     fake_SPI4;
SPI_TypeDef     fake_SPI5;

TIM_TypeDef     fake_TIM2;
TIM_TypeDef     fake_TIM3;
TIM_TypeDef     fake_TIM4;
TIM_TypeDef     fake_TIM5;

EXTI_TypeDef    fake_EXTI;
SYSCFG_TypeDef  fake_SYSCFG;
FLASH_TypeDef   fake_FLASH;

DMA_TypeDef     fake_DMA1;
DMA_TypeDef     fake_DMA2;

DMA_Stream_TypeDef fake_DMA1_S0;
DMA_Stream_TypeDef fake_DMA1_S1;
DMA_Stream_TypeDef fake_DMA1_S2;
DMA_Stream_TypeDef fake_DMA1_S3;
DMA_Stream_TypeDef fake_DMA1_S4;
DMA_Stream_TypeDef fake_DMA1_S5;
DMA_Stream_TypeDef fake_DMA1_S6;
DMA_Stream_TypeDef fake_DMA1_S7;
DMA_Stream_TypeDef fake_DMA2_S0;
DMA_Stream_TypeDef fake_DMA2_S1;
DMA_Stream_TypeDef fake_DMA2_S2;
DMA_Stream_TypeDef fake_DMA2_S3;
DMA_Stream_TypeDef fake_DMA2_S4;
DMA_Stream_TypeDef fake_DMA2_S5;
DMA_Stream_TypeDef fake_DMA2_S6;
DMA_Stream_TypeDef fake_DMA2_S7;

/* ---- Cortex-M4 core peripheral fake instances (declared in core_cm4.h) - */

NVIC_Type        fake_NVIC;
SCB_Type         fake_SCB;
SysTick_Type     fake_SysTick;
DWT_Type         fake_DWT;
CoreDebug_Type   fake_CoreDebug;

/* ---- BASEPRI fake (declared in core_cm4.h stub) ------------------------- */
uint32_t fake_BASEPRI = 0;

/* ---- System clock (declared in system_stm32f4xx.h) --------------------- */

uint32_t SystemCoreClock = 100000000U;  /* 100 MHz */

void SystemInit(void)             {}
void SystemCoreClockUpdate(void)  {}

/* ---- Reset all fake peripherals to power-on defaults (all zeros) -------- */

void test_periph_reset(void)
{
    memset(&fake_GPIOA,  0, sizeof(fake_GPIOA));
    memset(&fake_GPIOB,  0, sizeof(fake_GPIOB));
    memset(&fake_GPIOC,  0, sizeof(fake_GPIOC));
    memset(&fake_GPIOD,  0, sizeof(fake_GPIOD));
    memset(&fake_GPIOE,  0, sizeof(fake_GPIOE));
    memset(&fake_GPIOH,  0, sizeof(fake_GPIOH));

    memset(&fake_RCC,    0, sizeof(fake_RCC));
    memset(&fake_USART2, 0, sizeof(fake_USART2));

    memset(&fake_SPI1,   0, sizeof(fake_SPI1));
    memset(&fake_SPI2,   0, sizeof(fake_SPI2));
    memset(&fake_SPI3,   0, sizeof(fake_SPI3));
    memset(&fake_SPI4,   0, sizeof(fake_SPI4));
    memset(&fake_SPI5,   0, sizeof(fake_SPI5));

    memset(&fake_TIM2,   0, sizeof(fake_TIM2));
    memset(&fake_TIM3,   0, sizeof(fake_TIM3));
    memset(&fake_TIM4,   0, sizeof(fake_TIM4));
    memset(&fake_TIM5,   0, sizeof(fake_TIM5));

    memset(&fake_EXTI,   0, sizeof(fake_EXTI));
    memset(&fake_SYSCFG, 0, sizeof(fake_SYSCFG));
    memset(&fake_FLASH,  0, sizeof(fake_FLASH));

    memset(&fake_DMA1,   0, sizeof(fake_DMA1));
    memset(&fake_DMA2,   0, sizeof(fake_DMA2));

    memset(&fake_DMA1_S0, 0, sizeof(fake_DMA1_S0));
    memset(&fake_DMA1_S1, 0, sizeof(fake_DMA1_S1));
    memset(&fake_DMA1_S2, 0, sizeof(fake_DMA1_S2));
    memset(&fake_DMA1_S3, 0, sizeof(fake_DMA1_S3));
    memset(&fake_DMA1_S4, 0, sizeof(fake_DMA1_S4));
    memset(&fake_DMA1_S5, 0, sizeof(fake_DMA1_S5));
    memset(&fake_DMA1_S6, 0, sizeof(fake_DMA1_S6));
    memset(&fake_DMA1_S7, 0, sizeof(fake_DMA1_S7));
    memset(&fake_DMA2_S0, 0, sizeof(fake_DMA2_S0));
    memset(&fake_DMA2_S1, 0, sizeof(fake_DMA2_S1));
    memset(&fake_DMA2_S2, 0, sizeof(fake_DMA2_S2));
    memset(&fake_DMA2_S3, 0, sizeof(fake_DMA2_S3));
    memset(&fake_DMA2_S4, 0, sizeof(fake_DMA2_S4));
    memset(&fake_DMA2_S5, 0, sizeof(fake_DMA2_S5));
    memset(&fake_DMA2_S6, 0, sizeof(fake_DMA2_S6));
    memset(&fake_DMA2_S7, 0, sizeof(fake_DMA2_S7));

    memset(&fake_NVIC,       0, sizeof(fake_NVIC));
    memset(&fake_SCB,        0, sizeof(fake_SCB));
    memset(&fake_SysTick,    0, sizeof(fake_SysTick));
    memset(&fake_DWT,        0, sizeof(fake_DWT));
    memset(&fake_CoreDebug,  0, sizeof(fake_CoreDebug));

    fake_BASEPRI = 0;
}
