/**
 * @file test_harness.c
 * @brief Unity test harness for hardware-in-the-loop testing
 *
 * This file contains Unity-based tests that run on the STM32 target hardware.
 * Only compiled when HIL_TEST_MODE is defined (via HIL_TEST=1 build flag).
 *
 * Test coverage strategy:
 *
 *   Tier 1 — Smoke tests: all 5 SPI interfaces at max speed (prescaler 2,
 *            256 bytes), both polled and DMA. Verifies every SPI instance
 *            is functional with loopback cables.
 *
 *   Tier 2 — Deep sweep on one SPI per bus:
 *            - SPI2 (APB1, 50 MHz bus → 25 MHz SPI max)
 *            - SPI1 (APB2, 100 MHz bus → 50 MHz SPI max)
 *            Covers multiple prescaler values and buffer sizes to
 *            characterize throughput across the operating range.
 *
 *   Tier 3 — Non-SPI hardware tests (FPU, etc.)
 *
 *   Tier 4 — RCC clock tree and Timer accuracy tests.
 *
 *   Tier 5 — UART, GPIO output/input loopback and EXTI interrupt tests.
 *            GPIO loopback pairs (same wires as UART loopback):
 *              PA9 (output) <-> PB7 (input)  — UART1 loopback cable
 *              PC6 (output) <-> PC7 (input)  — UART6 loopback cable
 *            EXTI tests use PA9→PB7 pair (EXTI line 7, port B):
 *              rising edge, falling edge, and software trigger.
 */

#ifdef HIL_TEST_MODE

#include "unity.h"
#include "spi_perf.h"
#include "test_output.h"
#include "printf.h"
#include "printf_dma.h"
#include "rcc.h"
#include "timer.h"
#include "gpio_handler.h"
#include "exti_handler.h"
#include "stm32f4xx.h"  /* DWT / CoreDebug for cycle counting */

/* ====================================================================
 * UART loopback test helpers
 *
 * Two loopback pairs are wired on the board:
 *   USART1: PA9 (TX, AF7) <-> PB7 (RX, AF7)
 *   USART6: PC6 (TX, AF8) <-> PC7 (RX, AF8)
 *
 * Both UARTs are on APB2 (100 MHz).
 * BRR = (100_000_000 + 57_600) / 115_200 = 868.
 *
 * Tests use polled TX/RX with a generous timeout guard so that a
 * disconnected jumper produces a clean FAIL rather than a hang.
 * ==================================================================== */

#define UART_LB_SR_TXE    (1U << 7)
#define UART_LB_SR_RXNE   (1U << 5)
#define UART_LB_CR1_UE    (1U << 13)
#define UART_LB_CR1_TE    (1U << 3)
#define UART_LB_CR1_RE    (1U << 2)
#define UART_LB_BRR       868U          /* 115200 baud @ APB2 100 MHz */
#define UART_LB_TIMEOUT   1000000U      /* ~10 ms spin at 100 MHz     */

static void uart_lb_init_usart1(void)
{
    /* Clocks: GPIOA (PA9), GPIOB (PB7), USART1 */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    /* PA9: AF7 (USART1_TX) — MODER bits [19:18] = 10, AFR[1] bits [7:4] = 7 */
    GPIOA->MODER  = (GPIOA->MODER  & ~(3U << 18)) | (2U << 18);
    GPIOA->AFR[1] = (GPIOA->AFR[1] & ~(0xFU << 4)) | (7U << 4);

    /* PB7: AF7 (USART1_RX) — MODER bits [15:14] = 10, AFR[0] bits [31:28] = 7 */
    GPIOB->MODER  = (GPIOB->MODER  & ~(3U << 14)) | (2U << 14);
    GPIOB->AFR[0] = (GPIOB->AFR[0] & ~(0xFU << 28)) | (7U << 28);

    /* USART1: 115200 baud, 8N1, TX+RX enabled */
    USART1->CR1 = 0;
    USART1->BRR = UART_LB_BRR;
    USART1->CR1 = UART_LB_CR1_TE | UART_LB_CR1_RE | UART_LB_CR1_UE;
}

static void uart_lb_init_usart6(void)
{
    /* Clocks: GPIOC (PC6, PC7), USART6 */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART6EN;

    /* PC6: AF8 (USART6_TX) — MODER bits [13:12] = 10, AFR[0] bits [27:24] = 8 */
    GPIOC->MODER  = (GPIOC->MODER  & ~(3U << 12)) | (2U << 12);
    GPIOC->AFR[0] = (GPIOC->AFR[0] & ~(0xFU << 24)) | (8U << 24);

    /* PC7: AF8 (USART6_RX) — MODER bits [15:14] = 10, AFR[0] bits [31:28] = 8 */
    GPIOC->MODER  = (GPIOC->MODER  & ~(3U << 14)) | (2U << 14);
    GPIOC->AFR[0] = (GPIOC->AFR[0] & ~(0xFU << 28)) | (8U << 28);

    /* USART6: 115200 baud, 8N1, TX+RX enabled */
    USART6->CR1 = 0;
    USART6->BRR = UART_LB_BRR;
    USART6->CR1 = UART_LB_CR1_TE | UART_LB_CR1_RE | UART_LB_CR1_UE;
}

/* Returns 1 on success, 0 on timeout */
static int uart_lb_send(USART_TypeDef *u, uint8_t byte)
{
    uint32_t n = UART_LB_TIMEOUT;
    while (!(u->SR & UART_LB_SR_TXE)) { if (!--n) return 0; }
    u->DR = byte;
    return 1;
}

/* Returns 1 on success, 0 on timeout */
static int uart_lb_recv(USART_TypeDef *u, uint8_t *out)
{
    uint32_t n = UART_LB_TIMEOUT;
    while (!(u->SR & UART_LB_SR_RXNE)) { if (!--n) return 0; }
    *out = (uint8_t)(u->DR & 0xFFU);
    return 1;
}

/* Send one byte and receive it back; assert both operations succeed and
 * that the received value matches what was sent. */
static void uart_lb_check(USART_TypeDef *u, uint8_t byte)
{
    uint8_t rx = ~byte;
    TEST_ASSERT_MESSAGE(uart_lb_send(u, byte), "TX timeout — check jumper");
    TEST_ASSERT_MESSAGE(uart_lb_recv(u, &rx),  "RX timeout — check jumper");
    TEST_ASSERT_EQUAL_HEX8(byte, rx);
}

/* ====================================================================
 * Parameterized SPI test infrastructure
 * ==================================================================== */

typedef struct {
    spi_instance_t instance;
    uint16_t       prescaler;
    uint16_t       buffer_size;
    uint8_t        use_dma;
} spi_test_params_t;

static spi_test_params_t current_spi_params;

static void run_spi_test(void) {
    int result = spi_perf_run(
        current_spi_params.instance,
        current_spi_params.prescaler,
        current_spi_params.buffer_size,
        current_spi_params.use_dma);
    TEST_ASSERT_EQUAL_MESSAGE(0, result, "spi_perf_run failed");
}

/**
 * @brief Helper macro to define and run a parameterized SPI test.
 *
 * Unity's RUN_TEST requires a void(void) function pointer and captures
 * __LINE__ for reporting.  We store parameters in a static struct and
 * call a common runner.  The macro expands to a single RUN_TEST call
 * so Unity reports the correct line number.
 */
#define RUN_SPI_TEST(inst, psc, bufsz, dma) do {           \
    current_spi_params.instance    = (inst);                \
    current_spi_params.prescaler   = (psc);                 \
    current_spi_params.buffer_size = (bufsz);               \
    current_spi_params.use_dma     = (dma);                 \
    RUN_TEST(run_spi_test);                                 \
} while (0)

/* ====================================================================
 * Unity setup / teardown
 * ==================================================================== */

void setUp(void) {
    printf_dma_flush();
}

void tearDown(void) {
    printf_dma_flush();
}

/* ====================================================================
 * RCC clock frequency tests
 * ==================================================================== */

/*
 * These tests verify the clock tree configured by SystemInit() at
 * startup: 100 MHz SYSCLK from HSI via PLL, APB1 /2 → 50 MHz,
 * APB2 /1 → 100 MHz, APB1 timer clock = APB1 × 2 = 100 MHz.
 */

void test_rcc_sysclk_is_100mhz(void)
{
    TEST_ASSERT_EQUAL_UINT32(100000000U, rcc_get_sysclk());
}

void test_rcc_apb1_clk_is_50mhz(void)
{
    TEST_ASSERT_EQUAL_UINT32(50000000U, rcc_get_apb1_clk());
}

void test_rcc_apb2_clk_is_100mhz(void)
{
    TEST_ASSERT_EQUAL_UINT32(100000000U, rcc_get_apb2_clk());
}

void test_rcc_apb1_timer_clk_is_100mhz(void)
{
    /* APB1 prescaler = /2 → timer clock = APB1 × 2 per STM32 clock tree */
    TEST_ASSERT_EQUAL_UINT32(100000000U, rcc_get_apb1_timer_clk());
}

/* ====================================================================
 * Timer hardware tests
 * ==================================================================== */

/*
 * Measure timer_delay_us(1000) against the DWT cycle counter.
 *
 * At 100 MHz, 1000 µs = 100 000 cycles. We allow ±2000 cycles (±20 µs)
 * to accommodate DWT read overhead and minor timer startup latency.
 */
#define DELAY_TEST_US           1000U
#define DELAY_EXPECTED_CYCLES   (DELAY_TEST_US * 100U)  /* 100 000 at 100 MHz */
#define DELAY_TOLERANCE_CYCLES  2000U

void test_timer_delay_us_accuracy(void)
{
    /* Enable DWT cycle counter (same pattern as spi_perf.c) */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

    uint32_t start = DWT->CYCCNT;
    timer_delay_us(DELAY_TEST_US);
    uint32_t elapsed = DWT->CYCCNT - start;

    TEST_ASSERT_UINT32_WITHIN_MESSAGE(
        DELAY_TOLERANCE_CYCLES,
        DELAY_EXPECTED_CYCLES,
        elapsed,
        "timer_delay_us(1000) not within ±20 us of 1 ms");
}

/* ====================================================================
 * Non-SPI hardware tests
 * ==================================================================== */

void test_fpu_multiplication(void) {
    volatile float a = 3.14f;
    volatile float b = 2.72f;
    volatile float result = a * b;
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 8.54f, result,
                                     "FPU multiplication incorrect");
}

void test_fpu_division(void) {
    volatile float a = 10.0f;
    volatile float b = 4.0f;
    volatile float result = a / b;
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.001f, 2.5f, result,
                                     "FPU division incorrect");
}

/* ====================================================================
 * USART1 loopback tests (PA9=TX/AF7, PB7=RX/AF7)
 * ==================================================================== */

void test_usart1_loopback_0xa5(void)
{
    uart_lb_init_usart1();
    uart_lb_check(USART1, 0xA5);
}

void test_usart1_loopback_0x00(void)
{
    uart_lb_init_usart1();
    uart_lb_check(USART1, 0x00);
}

void test_usart1_loopback_0xff(void)
{
    uart_lb_init_usart1();
    uart_lb_check(USART1, 0xFF);
}

void test_usart1_loopback_sequence(void)
{
    uart_lb_init_usart1();
    /* Walking-bit and alternating patterns */
    static const uint8_t seq[] = { 0xAA, 0x55, 0x01, 0x02, 0x04, 0x08,
                                   0x10, 0x20, 0x40, 0x80, 0x12, 0x34 };
    for (uint32_t i = 0; i < sizeof(seq); i++) {
        uart_lb_check(USART1, seq[i]);
    }
}

/* ====================================================================
 * USART6 loopback tests (PC6=TX/AF8, PC7=RX/AF8)
 * ==================================================================== */

void test_usart6_loopback_0xa5(void)
{
    uart_lb_init_usart6();
    uart_lb_check(USART6, 0xA5);
}

void test_usart6_loopback_0x00(void)
{
    uart_lb_init_usart6();
    uart_lb_check(USART6, 0x00);
}

void test_usart6_loopback_0xff(void)
{
    uart_lb_init_usart6();
    uart_lb_check(USART6, 0xFF);
}

void test_usart6_loopback_sequence(void)
{
    uart_lb_init_usart6();
    static const uint8_t seq[] = { 0xAA, 0x55, 0x01, 0x02, 0x04, 0x08,
                                   0x10, 0x20, 0x40, 0x80, 0x12, 0x34 };
    for (uint32_t i = 0; i < sizeof(seq); i++) {
        uart_lb_check(USART6, seq[i]);
    }
}

/* ====================================================================
 * GPIO output/input loopback test helpers
 *
 * Loopback wiring (same jumper cables used for UART loopback tests):
 *   PA9 (output) <-> PB7 (input)   — UART1 cable
 *   PC6 (output) <-> PC7 (input)   — UART6 cable
 *
 * Each test reconfigures the pins as plain push-pull GPIO output and
 * floating input, drives the output, and reads back the input.  Pins
 * are cleaned up (re-set to analog/hi-Z) after each test so they do
 * not interfere with the UART loopback tests that follow.
 * ==================================================================== */

/* Short busy-wait: ~10 µs at 100 MHz — allows the signal to propagate
 * through the loopback cable before we sample the input pin.           */
#define GPIO_LB_SETTLE_CYCLES  1000U

static void gpio_lb_settle(void)
{
    volatile uint32_t n = GPIO_LB_SETTLE_CYCLES;
    while (n--) { /* spin */ }
}

/**
 * @brief Configure the PA9/PB7 loopback pair as plain GPIO output/input.
 *
 * PA9 — push-pull output, no pull
 * PB7 — floating input, no pull
 */
static void gpio_lb_init_pa9_pb7(void)
{
    gpio_clock_enable(GPIO_PORT_A);
    gpio_clock_enable(GPIO_PORT_B);

    gpio_configure_full(GPIO_PORT_A, 9,
                        GPIO_MODE_OUTPUT, GPIO_OUTPUT_PUSH_PULL,
                        GPIO_SPEED_FAST, GPIO_PULL_NONE);

    gpio_configure_full(GPIO_PORT_B, 7,
                        GPIO_MODE_INPUT, GPIO_OUTPUT_PUSH_PULL,
                        GPIO_SPEED_LOW, GPIO_PULL_NONE);
}

/**
 * @brief Release the PA9/PB7 pair back to analog (hi-Z).
 * Called after each GPIO test so UART tests on the same pins work.
 */
static void gpio_lb_deinit_pa9_pb7(void)
{
    gpio_configure_pin(GPIO_PORT_A, 9, GPIO_MODE_ANALOG);
    gpio_configure_pin(GPIO_PORT_B, 7, GPIO_MODE_ANALOG);
}

/**
 * @brief Configure the PC6/PC7 loopback pair as plain GPIO output/input.
 *
 * PC6 — push-pull output, no pull
 * PC7 — floating input, no pull
 */
static void gpio_lb_init_pc6_pc7(void)
{
    gpio_clock_enable(GPIO_PORT_C);

    gpio_configure_full(GPIO_PORT_C, 6,
                        GPIO_MODE_OUTPUT, GPIO_OUTPUT_PUSH_PULL,
                        GPIO_SPEED_FAST, GPIO_PULL_NONE);

    gpio_configure_full(GPIO_PORT_C, 7,
                        GPIO_MODE_INPUT, GPIO_OUTPUT_PUSH_PULL,
                        GPIO_SPEED_LOW, GPIO_PULL_NONE);
}

/**
 * @brief Release the PC6/PC7 pair back to analog (hi-Z).
 */
static void gpio_lb_deinit_pc6_pc7(void)
{
    gpio_configure_pin(GPIO_PORT_C, 6, GPIO_MODE_ANALOG);
    gpio_configure_pin(GPIO_PORT_C, 7, GPIO_MODE_ANALOG);
}

/* ====================================================================
 * GPIO loopback tests — PA9 output / PB7 input
 * ==================================================================== */

void test_gpio_pa9_high_reads_pb7_high(void)
{
    gpio_lb_init_pa9_pb7();
    gpio_set_pin(GPIO_PORT_A, 9);
    gpio_lb_settle();
    uint8_t level = gpio_read_pin(GPIO_PORT_B, 7);
    gpio_lb_deinit_pa9_pb7();
    TEST_ASSERT_EQUAL_MESSAGE(1, level,
        "PA9=HIGH should read PB7=HIGH — check jumper PA9<->PB7");
}

void test_gpio_pa9_low_reads_pb7_low(void)
{
    gpio_lb_init_pa9_pb7();
    gpio_clear_pin(GPIO_PORT_A, 9);
    gpio_lb_settle();
    uint8_t level = gpio_read_pin(GPIO_PORT_B, 7);
    gpio_lb_deinit_pa9_pb7();
    TEST_ASSERT_EQUAL_MESSAGE(0, level,
        "PA9=LOW should read PB7=LOW — check jumper PA9<->PB7");
}

void test_gpio_pa9_toggle_reads_back(void)
{
    gpio_lb_init_pa9_pb7();

    /* Start LOW */
    gpio_clear_pin(GPIO_PORT_A, 9);
    gpio_lb_settle();
    TEST_ASSERT_EQUAL_MESSAGE(0, gpio_read_pin(GPIO_PORT_B, 7),
        "PA9 start LOW: PB7 should be LOW");

    /* Toggle to HIGH */
    gpio_toggle_pin(GPIO_PORT_A, 9);
    gpio_lb_settle();
    TEST_ASSERT_EQUAL_MESSAGE(1, gpio_read_pin(GPIO_PORT_B, 7),
        "PA9 after toggle: PB7 should be HIGH");

    /* Toggle back to LOW */
    gpio_toggle_pin(GPIO_PORT_A, 9);
    gpio_lb_settle();
    TEST_ASSERT_EQUAL_MESSAGE(0, gpio_read_pin(GPIO_PORT_B, 7),
        "PA9 after second toggle: PB7 should be LOW");

    gpio_lb_deinit_pa9_pb7();
}

/* ====================================================================
 * GPIO loopback tests — PC6 output / PC7 input
 * ==================================================================== */

void test_gpio_pc6_high_reads_pc7_high(void)
{
    gpio_lb_init_pc6_pc7();
    gpio_set_pin(GPIO_PORT_C, 6);
    gpio_lb_settle();
    uint8_t level = gpio_read_pin(GPIO_PORT_C, 7);
    gpio_lb_deinit_pc6_pc7();
    TEST_ASSERT_EQUAL_MESSAGE(1, level,
        "PC6=HIGH should read PC7=HIGH — check jumper PC6<->PC7");
}

void test_gpio_pc6_low_reads_pc7_low(void)
{
    gpio_lb_init_pc6_pc7();
    gpio_clear_pin(GPIO_PORT_C, 6);
    gpio_lb_settle();
    uint8_t level = gpio_read_pin(GPIO_PORT_C, 7);
    gpio_lb_deinit_pc6_pc7();
    TEST_ASSERT_EQUAL_MESSAGE(0, level,
        "PC6=LOW should read PC7=LOW — check jumper PC6<->PC7");
}

/* ====================================================================
 * EXTI interrupt tests — PA9 output triggers EXTI line 7 (port B, PB7)
 *
 * Strategy: configure lines in EXTI_MODE_EVENT (EMR set, IMR cleared).
 * In event mode the EXTI->PR pending bit is still set on the selected
 * edge, but no interrupt request reaches the NVIC — so the CPU never
 * vectors to EXTI9_5_IRQHandler and the Default_Handler (infinite loop)
 * is never entered.  We poll EXTI->PR directly to observe each edge.
 *
 * Note: exti_configure_gpio_interrupt() always enables the NVIC IRQ
 * regardless of mode.  That is safe here because with IMR=0 the NVIC
 * interrupt request is gated at the EXTI peripheral; the NVIC enable
 * bit has no effect while IMR is clear.
 *
 * Timeout guard: spin up to ~1 ms (100 000 cycles at 100 MHz) before
 * declaring a failure — long enough for any reasonable signal path but
 * short enough to avoid a multi-second hang on a disconnected jumper.
 * ==================================================================== */

#define EXTI_LB_TIMEOUT_CYCLES  100000U

/**
 * @brief Wait up to EXTI_LB_TIMEOUT_CYCLES for EXTI->PR bit to be set.
 * @return 1 if pending bit set within timeout, 0 on timeout.
 */
static int exti_lb_wait_pending(uint8_t line)
{
    uint32_t n = EXTI_LB_TIMEOUT_CYCLES;
    while (n--) {
        if (exti_is_pending(line)) return 1;
    }
    return 0;
}

void test_exti_rising_edge_pb7(void)
{
    /* PA9 = output (drive signal), PB7 = input (EXTI line 7 port B)
     * Use EVENT mode so PR is set on edge but no NVIC interrupt fires. */
    gpio_lb_init_pa9_pb7();

    /* Drive output LOW before arming the edge detector */
    gpio_clear_pin(GPIO_PORT_A, 9);
    gpio_lb_settle();

    int ret = exti_configure_gpio_interrupt(GPIO_PORT_B, 7,
                                            EXTI_TRIGGER_RISING,
                                            EXTI_MODE_EVENT);
    TEST_ASSERT_EQUAL_MESSAGE(0, ret,
        "exti_configure_gpio_interrupt(PB7, RISING, EVENT) failed");

    /* Clear any stale pending flag */
    exti_clear_pending(7);

    /* Trigger: drive PA9 HIGH (rising edge on PB7) */
    gpio_set_pin(GPIO_PORT_A, 9);

    /* Observe: EXTI->PR bit 7 should be set */
    int fired = exti_lb_wait_pending(7);

    /* Cleanup */
    exti_clear_pending(7);
    exti_disable_line(7);
    exti_set_event_mask(7, 0);
    gpio_lb_deinit_pa9_pb7();

    TEST_ASSERT_MESSAGE(fired,
        "EXTI line 7 rising edge did not fire — check jumper PA9<->PB7");
}

void test_exti_falling_edge_pb7(void)
{
    /* Start with PA9 HIGH so a falling edge can be generated */
    gpio_lb_init_pa9_pb7();
    gpio_set_pin(GPIO_PORT_A, 9);
    gpio_lb_settle();

    int ret = exti_configure_gpio_interrupt(GPIO_PORT_B, 7,
                                            EXTI_TRIGGER_FALLING,
                                            EXTI_MODE_EVENT);
    TEST_ASSERT_EQUAL_MESSAGE(0, ret,
        "exti_configure_gpio_interrupt(PB7, FALLING, EVENT) failed");

    exti_clear_pending(7);

    /* Drive PA9 LOW → falling edge on PB7 */
    gpio_clear_pin(GPIO_PORT_A, 9);

    int fired = exti_lb_wait_pending(7);

    exti_clear_pending(7);
    exti_disable_line(7);
    exti_set_event_mask(7, 0);
    gpio_lb_deinit_pa9_pb7();

    TEST_ASSERT_MESSAGE(fired,
        "EXTI line 7 falling edge did not fire — check jumper PA9<->PB7");
}

void test_exti_both_edges_pb7(void)
{
    gpio_lb_init_pa9_pb7();
    gpio_clear_pin(GPIO_PORT_A, 9);
    gpio_lb_settle();

    int ret = exti_configure_gpio_interrupt(GPIO_PORT_B, 7,
                                            EXTI_TRIGGER_BOTH,
                                            EXTI_MODE_EVENT);
    TEST_ASSERT_EQUAL_MESSAGE(0, ret,
        "exti_configure_gpio_interrupt(PB7, BOTH, EVENT) failed");

    exti_clear_pending(7);

    /* Rising edge */
    gpio_set_pin(GPIO_PORT_A, 9);
    int fired_rising = exti_lb_wait_pending(7);
    exti_clear_pending(7);

    /* Falling edge */
    gpio_clear_pin(GPIO_PORT_A, 9);
    int fired_falling = exti_lb_wait_pending(7);
    exti_clear_pending(7);

    exti_disable_line(7);
    exti_set_event_mask(7, 0);
    gpio_lb_deinit_pa9_pb7();

    TEST_ASSERT_MESSAGE(fired_rising,
        "EXTI BOTH: rising edge did not fire — check jumper PA9<->PB7");
    TEST_ASSERT_MESSAGE(fired_falling,
        "EXTI BOTH: falling edge did not fire — check jumper PA9<->PB7");
}

void test_exti_software_trigger_pb7(void)
{
    /* Software trigger test: no loopback cable needed.
     * EXTI->SWIER sets the PR pending bit when IMR or EMR is enabled.
     * Use EVENT mode (EMR set, IMR cleared) to avoid Default_Handler. */
    gpio_clock_enable(GPIO_PORT_B);
    gpio_configure_pin(GPIO_PORT_B, 7, GPIO_MODE_INPUT);

    /* Enable SYSCFG clock (needed by exti_configure_gpio_interrupt) */
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;

    /* Configure for rising edge, event mode, so SWIER can set PR */
    int ret = exti_configure_gpio_interrupt(GPIO_PORT_B, 7,
                                            EXTI_TRIGGER_RISING,
                                            EXTI_MODE_EVENT);
    TEST_ASSERT_EQUAL_MESSAGE(0, ret,
        "exti_configure_gpio_interrupt for software trigger test failed");

    exti_clear_pending(7);

    /* Fire software trigger */
    int sw_ret = exti_software_trigger(7);
    TEST_ASSERT_EQUAL_MESSAGE(0, sw_ret,
        "exti_software_trigger(7) returned error");

    /* Check pending bit was set */
    int fired = exti_is_pending(7);

    /* Cleanup */
    exti_clear_pending(7);
    exti_disable_line(7);
    exti_set_event_mask(7, 0);
    gpio_configure_pin(GPIO_PORT_B, 7, GPIO_MODE_ANALOG);

    TEST_ASSERT_MESSAGE(fired,
        "EXTI software trigger: pending bit was not set");
}

/* ====================================================================
 * Main test runner
 * ==================================================================== */

int run_unity_tests(void) {
    UNITY_BEGIN();

    /* ----------------------------------------------------------
     * Tier 1: Smoke test — all 5 SPIs at max speed
     *   prescaler=2, 256 bytes, polled + DMA
     * ---------------------------------------------------------- */
    printf("\n--- Tier 1: All-SPI smoke (psc=2, 256B) ---\n");
    printf_dma_flush();

    RUN_SPI_TEST(SPI_INSTANCE_1, 2, 256, 0);   /* SPI1 polled */
    RUN_SPI_TEST(SPI_INSTANCE_1, 2, 256, 1);   /* SPI1 DMA    */
    RUN_SPI_TEST(SPI_INSTANCE_2, 2, 256, 0);   /* SPI2 polled */
    RUN_SPI_TEST(SPI_INSTANCE_2, 2, 256, 1);   /* SPI2 DMA    */
    RUN_SPI_TEST(SPI_INSTANCE_3, 2, 256, 0);   /* SPI3 polled */
    RUN_SPI_TEST(SPI_INSTANCE_3, 2, 256, 1);   /* SPI3 DMA    */
    RUN_SPI_TEST(SPI_INSTANCE_4, 2, 256, 0);   /* SPI4 polled */
    RUN_SPI_TEST(SPI_INSTANCE_4, 2, 256, 1);   /* SPI4 DMA    */
    RUN_SPI_TEST(SPI_INSTANCE_5, 2, 256, 0);   /* SPI5 polled */
    RUN_SPI_TEST(SPI_INSTANCE_5, 2, 256, 1);   /* SPI5 DMA    */

    /* ----------------------------------------------------------
     * Tier 2a: Deep sweep — SPI2 (APB1, 50 MHz bus)
     *   Prescaler sweep: 2, 4, 8, 16, 32, 64, 128, 256
     *   Buffer sizes: 1, 4, 16, 64, 256
     *   Both polled and DMA
     *
     *   Full matrix is 8×5×2 = 80 tests. We select a representative
     *   subset: all prescalers at 256B, all sizes at prescaler=2,
     *   plus DMA mirrors. This gives thorough coverage with ~30 tests.
     * ---------------------------------------------------------- */
    printf("\n--- Tier 2a: SPI2 (APB1 50MHz) deep sweep ---\n");
    printf_dma_flush();

    /* Prescaler sweep at 256 bytes — polled */
    RUN_SPI_TEST(SPI_INSTANCE_2, 2,   256, 0);
    RUN_SPI_TEST(SPI_INSTANCE_2, 4,   256, 0);
    RUN_SPI_TEST(SPI_INSTANCE_2, 8,   256, 0);
    RUN_SPI_TEST(SPI_INSTANCE_2, 16,  256, 0);
    RUN_SPI_TEST(SPI_INSTANCE_2, 32,  256, 0);
    RUN_SPI_TEST(SPI_INSTANCE_2, 64,  256, 0);
    RUN_SPI_TEST(SPI_INSTANCE_2, 128, 256, 0);
    RUN_SPI_TEST(SPI_INSTANCE_2, 256, 256, 0);

    /* Prescaler sweep at 256 bytes — DMA */
    RUN_SPI_TEST(SPI_INSTANCE_2, 2,   256, 1);
    RUN_SPI_TEST(SPI_INSTANCE_2, 4,   256, 1);
    RUN_SPI_TEST(SPI_INSTANCE_2, 8,   256, 1);
    RUN_SPI_TEST(SPI_INSTANCE_2, 16,  256, 1);
    RUN_SPI_TEST(SPI_INSTANCE_2, 32,  256, 1);
    RUN_SPI_TEST(SPI_INSTANCE_2, 64,  256, 1);
    RUN_SPI_TEST(SPI_INSTANCE_2, 128, 256, 1);
    RUN_SPI_TEST(SPI_INSTANCE_2, 256, 256, 1);

    /* Buffer size sweep at prescaler=2 — polled */
    RUN_SPI_TEST(SPI_INSTANCE_2, 2, 1,   0);
    RUN_SPI_TEST(SPI_INSTANCE_2, 2, 4,   0);
    RUN_SPI_TEST(SPI_INSTANCE_2, 2, 16,  0);
    RUN_SPI_TEST(SPI_INSTANCE_2, 2, 64,  0);

    /* Buffer size sweep at prescaler=2 — DMA */
    RUN_SPI_TEST(SPI_INSTANCE_2, 2, 1,   1);
    RUN_SPI_TEST(SPI_INSTANCE_2, 2, 4,   1);
    RUN_SPI_TEST(SPI_INSTANCE_2, 2, 16,  1);
    RUN_SPI_TEST(SPI_INSTANCE_2, 2, 64,  1);

    /* ----------------------------------------------------------
     * Tier 2b: Deep sweep — SPI1 (APB2, 100 MHz bus)
     *   Same test matrix structure as SPI2.
     * ---------------------------------------------------------- */
    printf("\n--- Tier 2b: SPI1 (APB2 100MHz) deep sweep ---\n");
    printf_dma_flush();

    /* Prescaler sweep at 256 bytes — polled */
    RUN_SPI_TEST(SPI_INSTANCE_1, 2,   256, 0);
    RUN_SPI_TEST(SPI_INSTANCE_1, 4,   256, 0);
    RUN_SPI_TEST(SPI_INSTANCE_1, 8,   256, 0);
    RUN_SPI_TEST(SPI_INSTANCE_1, 16,  256, 0);
    RUN_SPI_TEST(SPI_INSTANCE_1, 32,  256, 0);
    RUN_SPI_TEST(SPI_INSTANCE_1, 64,  256, 0);
    RUN_SPI_TEST(SPI_INSTANCE_1, 128, 256, 0);
    RUN_SPI_TEST(SPI_INSTANCE_1, 256, 256, 0);

    /* Prescaler sweep at 256 bytes — DMA */
    RUN_SPI_TEST(SPI_INSTANCE_1, 2,   256, 1);
    RUN_SPI_TEST(SPI_INSTANCE_1, 4,   256, 1);
    RUN_SPI_TEST(SPI_INSTANCE_1, 8,   256, 1);
    RUN_SPI_TEST(SPI_INSTANCE_1, 16,  256, 1);
    RUN_SPI_TEST(SPI_INSTANCE_1, 32,  256, 1);
    RUN_SPI_TEST(SPI_INSTANCE_1, 64,  256, 1);
    RUN_SPI_TEST(SPI_INSTANCE_1, 128, 256, 1);
    RUN_SPI_TEST(SPI_INSTANCE_1, 256, 256, 1);

    /* Buffer size sweep at prescaler=2 — polled */
    RUN_SPI_TEST(SPI_INSTANCE_1, 2, 1,   0);
    RUN_SPI_TEST(SPI_INSTANCE_1, 2, 4,   0);
    RUN_SPI_TEST(SPI_INSTANCE_1, 2, 16,  0);
    RUN_SPI_TEST(SPI_INSTANCE_1, 2, 64,  0);

    /* Buffer size sweep at prescaler=2 — DMA */
    RUN_SPI_TEST(SPI_INSTANCE_1, 2, 1,   1);
    RUN_SPI_TEST(SPI_INSTANCE_1, 2, 4,   1);
    RUN_SPI_TEST(SPI_INSTANCE_1, 2, 16,  1);
    RUN_SPI_TEST(SPI_INSTANCE_1, 2, 64,  1);

    /* ----------------------------------------------------------
     * Tier 3: Non-SPI hardware tests
     * ---------------------------------------------------------- */
    printf("\n--- Tier 3: FPU tests ---\n");
    printf_dma_flush();

    RUN_TEST(test_fpu_multiplication);
    RUN_TEST(test_fpu_division);

    /* ----------------------------------------------------------
     * Tier 4: RCC and Timer hardware tests
     * ---------------------------------------------------------- */
    printf("\n--- Tier 4: RCC clock tree ---\n");
    printf_dma_flush();

    RUN_TEST(test_rcc_sysclk_is_100mhz);
    RUN_TEST(test_rcc_apb1_clk_is_50mhz);
    RUN_TEST(test_rcc_apb2_clk_is_100mhz);
    RUN_TEST(test_rcc_apb1_timer_clk_is_100mhz);

    printf("\n--- Tier 4: Timer delay accuracy ---\n");
    printf_dma_flush();

    RUN_TEST(test_timer_delay_us_accuracy);

    /* ----------------------------------------------------------
     * Tier 5: UART loopback hardware tests
     *   USART1: PA9 (TX/AF7) wired to PB7 (RX/AF7)
     *   USART6: PC6 (TX/AF8) wired to PC7 (RX/AF8)
     *   115200 baud, polled, no DMA/interrupts.
     * ---------------------------------------------------------- */
    printf("\n--- Tier 5: USART1 loopback (PA9/PB7) ---\n");
    printf_dma_flush();

    RUN_TEST(test_usart1_loopback_0xa5);
    RUN_TEST(test_usart1_loopback_0x00);
    RUN_TEST(test_usart1_loopback_0xff);
    RUN_TEST(test_usart1_loopback_sequence);

    printf("\n--- Tier 5: USART6 loopback (PC6/PC7) ---\n");
    printf_dma_flush();

    RUN_TEST(test_usart6_loopback_0xa5);
    RUN_TEST(test_usart6_loopback_0x00);
    RUN_TEST(test_usart6_loopback_0xff);
    RUN_TEST(test_usart6_loopback_sequence);

    /* ----------------------------------------------------------
     * Tier 5: GPIO output/input loopback tests
     *   PA9 (output) wired to PB7 (input) — UART1 loopback cable
     *   PC6 (output) wired to PC7 (input) — UART6 loopback cable
     * ---------------------------------------------------------- */
    printf("\n--- Tier 5: GPIO loopback (PA9/PB7) ---\n");
    printf_dma_flush();

    RUN_TEST(test_gpio_pa9_high_reads_pb7_high);
    RUN_TEST(test_gpio_pa9_low_reads_pb7_low);
    RUN_TEST(test_gpio_pa9_toggle_reads_back);

    printf("\n--- Tier 5: GPIO loopback (PC6/PC7) ---\n");
    printf_dma_flush();

    RUN_TEST(test_gpio_pc6_high_reads_pc7_high);
    RUN_TEST(test_gpio_pc6_low_reads_pc7_low);

    /* ----------------------------------------------------------
     * Tier 5: EXTI interrupt tests
     *   PA9 (output) wired to PB7 (input, EXTI line 7 port B)
     * ---------------------------------------------------------- */
    printf("\n--- Tier 5: EXTI loopback (PA9->PB7, line 7) ---\n");
    printf_dma_flush();

    RUN_TEST(test_exti_rising_edge_pb7);
    RUN_TEST(test_exti_falling_edge_pb7);
    RUN_TEST(test_exti_both_edges_pb7);
    RUN_TEST(test_exti_software_trigger_pb7);

    printf_dma_flush();
    return UNITY_END();
}

#endif /* HIL_TEST_MODE */
