/*
 * test_periph.h — Fake STM32 peripheral instances for driver host tests.
 *
 * Included by tests/driver_stubs/stm32f4xx.h AFTER the real stm32f411xe.h,
 * so all TypeDef types (GPIO_TypeDef, RCC_TypeDef, ...) are already defined.
 *
 * Two things this file does:
 *  1. Declares extern fake peripheral struct instances (defined in test_periph.c)
 *  2. #undef/#redefine all peripheral instance macros to point at those fakes
 *
 * Usage in test setUp():
 *   test_periph_reset();    // zero all fake structs before each test case
 *
 * Usage in assertions:
 *   TEST_ASSERT_BITS_HIGH(RCC_AHB1ENR_GPIOAEN, fake_RCC.AHB1ENR);
 *   TEST_ASSERT_EQUAL_HEX32(1U << 10, fake_GPIOA.MODER);
 */

#ifndef TEST_PERIPH_H
#define TEST_PERIPH_H

/* ---- STM32 peripheral fake instances ------------------------------------ */

extern GPIO_TypeDef    fake_GPIOA;
extern GPIO_TypeDef    fake_GPIOB;
extern GPIO_TypeDef    fake_GPIOC;
extern GPIO_TypeDef    fake_GPIOD;
extern GPIO_TypeDef    fake_GPIOE;
extern GPIO_TypeDef    fake_GPIOH;

extern RCC_TypeDef     fake_RCC;

extern USART_TypeDef   fake_USART1;
extern USART_TypeDef   fake_USART2;
extern USART_TypeDef   fake_USART6;

extern SPI_TypeDef     fake_SPI1;
extern SPI_TypeDef     fake_SPI2;
extern SPI_TypeDef     fake_SPI3;
extern SPI_TypeDef     fake_SPI4;
extern SPI_TypeDef     fake_SPI5;

extern TIM_TypeDef     fake_TIM2;
extern TIM_TypeDef     fake_TIM3;
extern TIM_TypeDef     fake_TIM4;
extern TIM_TypeDef     fake_TIM5;

extern EXTI_TypeDef    fake_EXTI;
extern SYSCFG_TypeDef  fake_SYSCFG;
extern FLASH_TypeDef   fake_FLASH;

extern DMA_TypeDef     fake_DMA1;
extern DMA_TypeDef     fake_DMA2;

extern DMA_Stream_TypeDef fake_DMA1_S0;
extern DMA_Stream_TypeDef fake_DMA1_S1;
extern DMA_Stream_TypeDef fake_DMA1_S2;
extern DMA_Stream_TypeDef fake_DMA1_S3;
extern DMA_Stream_TypeDef fake_DMA1_S4;
extern DMA_Stream_TypeDef fake_DMA1_S5;
extern DMA_Stream_TypeDef fake_DMA1_S6;
extern DMA_Stream_TypeDef fake_DMA1_S7;
extern DMA_Stream_TypeDef fake_DMA2_S0;
extern DMA_Stream_TypeDef fake_DMA2_S1;
extern DMA_Stream_TypeDef fake_DMA2_S2;
extern DMA_Stream_TypeDef fake_DMA2_S3;
extern DMA_Stream_TypeDef fake_DMA2_S4;
extern DMA_Stream_TypeDef fake_DMA2_S5;
extern DMA_Stream_TypeDef fake_DMA2_S6;
extern DMA_Stream_TypeDef fake_DMA2_S7;

/* ---- Cortex-M4 core peripheral fakes (declared in core_cm4.h stub) ------ */
/* fake_NVIC, fake_SCB, fake_SysTick, fake_DWT, fake_CoreDebug             */

/* ---- BASEPRI fake (declared in core_cm4.h stub) ------------------------- */
extern uint32_t fake_BASEPRI;

/* ---- Override STM32 peripheral instance macros -------------------------- */

#undef GPIOA
#define GPIOA    (&fake_GPIOA)
#undef GPIOB
#define GPIOB    (&fake_GPIOB)
#undef GPIOC
#define GPIOC    (&fake_GPIOC)
#undef GPIOD
#define GPIOD    (&fake_GPIOD)
#undef GPIOE
#define GPIOE    (&fake_GPIOE)
#undef GPIOH
#define GPIOH    (&fake_GPIOH)

#undef RCC
#define RCC      (&fake_RCC)

#undef USART1
#define USART1   (&fake_USART1)
#undef USART2
#define USART2   (&fake_USART2)
#undef USART6
#define USART6   (&fake_USART6)

#undef SPI1
#define SPI1     (&fake_SPI1)
#undef SPI2
#define SPI2     (&fake_SPI2)
#undef SPI3
#define SPI3     (&fake_SPI3)
#undef SPI4
#define SPI4     (&fake_SPI4)
#undef SPI5
#define SPI5     (&fake_SPI5)

#undef TIM2
#define TIM2     (&fake_TIM2)
#undef TIM3
#define TIM3     (&fake_TIM3)
#undef TIM4
#define TIM4     (&fake_TIM4)
#undef TIM5
#define TIM5     (&fake_TIM5)

#undef EXTI
#define EXTI     (&fake_EXTI)
#undef SYSCFG
#define SYSCFG   (&fake_SYSCFG)
#undef FLASH
#define FLASH    (&fake_FLASH)

#undef DMA1
#define DMA1     (&fake_DMA1)
#undef DMA2
#define DMA2     (&fake_DMA2)

#undef DMA1_Stream0
#define DMA1_Stream0  (&fake_DMA1_S0)
#undef DMA1_Stream1
#define DMA1_Stream1  (&fake_DMA1_S1)
#undef DMA1_Stream2
#define DMA1_Stream2  (&fake_DMA1_S2)
#undef DMA1_Stream3
#define DMA1_Stream3  (&fake_DMA1_S3)
#undef DMA1_Stream4
#define DMA1_Stream4  (&fake_DMA1_S4)
#undef DMA1_Stream5
#define DMA1_Stream5  (&fake_DMA1_S5)
#undef DMA1_Stream6
#define DMA1_Stream6  (&fake_DMA1_S6)
#undef DMA1_Stream7
#define DMA1_Stream7  (&fake_DMA1_S7)

#undef DMA2_Stream0
#define DMA2_Stream0  (&fake_DMA2_S0)
#undef DMA2_Stream1
#define DMA2_Stream1  (&fake_DMA2_S1)
#undef DMA2_Stream2
#define DMA2_Stream2  (&fake_DMA2_S2)
#undef DMA2_Stream3
#define DMA2_Stream3  (&fake_DMA2_S3)
#undef DMA2_Stream4
#define DMA2_Stream4  (&fake_DMA2_S4)
#undef DMA2_Stream5
#define DMA2_Stream5  (&fake_DMA2_S5)
#undef DMA2_Stream6
#define DMA2_Stream6  (&fake_DMA2_S6)
#undef DMA2_Stream7
#define DMA2_Stream7  (&fake_DMA2_S7)

/* ---- Reset helper ------------------------------------------------------- */

/**
 * @brief Zero all fake peripheral structs to power-on defaults.
 *
 * Call in setUp() before each test case to ensure a clean state.
 * Also resets Cortex-M4 core fakes (NVIC, SCB, SysTick, DWT, CoreDebug).
 */
void test_periph_reset(void);

#endif /* TEST_PERIPH_H */
