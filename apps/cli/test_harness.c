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
#include "systick.h"
#include "gpio_handler.h"
#include "exti_handler.h"
#include "flash.h"
#include "crc.h"
#include "sleep_mode.h"
#include "stm32f4xx.h"  /* DWT / CoreDebug for cycle counting */

/* Plan 002 B0.3 — software BPSK modem (Tier 9). */
#include "prbs.h"
#include "bpsk.h"
#include "awgn.h"
#include "fixed.h"
#include "rrc.h"

/* Plan 002 B0.5 — impairments + RX recovery loops (Tier 9c, #197). */
#include "complexq15.h"
#include "sincos.h"
#include "nco.h"
#include "impair.h"
#include "agc.h"
#include "timing_mm.h"
#include "costas.h"
#include "barker.h"

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
 * SysTick hardware tests
 * ==================================================================== */

/*
 * Verify systick_get_ms() advances over a 5 ms hardware delay.
 *
 * timer_delay_us(5000) burns ~500 000 cycles on the Cortex-M4 timer;
 * the SysTick ISR still fires every 1 ms, so the counter must increase
 * by at least 4 ms and at most 6 ms.
 */
void test_systick_get_ms_increments(void)
{
    uint32_t before = systick_get_ms();
    timer_delay_us(5000);
    uint32_t after  = systick_get_ms();
    uint32_t diff   = after - before;   /* unsigned: handles wrap correctly */

    TEST_ASSERT_UINT32_WITHIN_MESSAGE(
        1U,       /* tolerance: ±1 ms */
        5U,       /* expected centre */
        diff,
        "systick_get_ms() did not advance ~5 ms over a 5 ms delay");
}

/*
 * Verify systick_elapsed_since() returns the correct elapsed time.
 *
 * Record start, wait 10 ms via timer_delay_us(10000), then check the
 * returned elapsed value is within 9–11 ms.
 */
void test_systick_elapsed_since(void)
{
    uint32_t start = systick_get_ms();
    timer_delay_us(10000);
    uint32_t elapsed = systick_elapsed_since(start);

    TEST_ASSERT_UINT32_WITHIN_MESSAGE(
        1U,       /* tolerance: ±1 ms */
        10U,      /* expected centre */
        elapsed,
        "systick_elapsed_since() did not return ~10 ms after a 10 ms delay");
}

/*
 * Measure systick_delay_ms(10) against the DWT cycle counter.
 *
 * At 100 MHz, 10 ms = 1 000 000 cycles.  We allow ±100 000 cycles (±1 ms)
 * because systick_delay_ms() has inherent 1 ms quantisation: depending on
 * where in the current tick period systick_get_ms() is sampled, the actual
 * wait can be anywhere from 9 ms to 10 ms.  The tolerance is set to 1 full
 * SysTick period (100 000 cycles) to cover the worst-case phase offset while
 * still confirming the delay is in the right ballpark.
 */
#define SYSTICK_DELAY_TEST_MS          10U
#define SYSTICK_DELAY_EXPECTED_CYCLES  (SYSTICK_DELAY_TEST_MS * 100000U)  /* 1 000 000 */
#define SYSTICK_DELAY_TOLERANCE_CYCLES 100000U  /* ±1 ms quantisation */

void test_systick_delay_ms_accuracy(void)
{
    /* Enable DWT cycle counter */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

    uint32_t start = DWT->CYCCNT;
    systick_delay_ms(SYSTICK_DELAY_TEST_MS);
    uint32_t elapsed = DWT->CYCCNT - start;

    TEST_ASSERT_UINT32_WITHIN_MESSAGE(
        SYSTICK_DELAY_TOLERANCE_CYCLES,
        SYSTICK_DELAY_EXPECTED_CYCLES,
        elapsed,
        "systick_delay_ms(10) not within ±1 ms of 10 ms");
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
 * GPIO loopback tests
 *
 * Each function covers one loopback pair end-to-end: HIGH, LOW, and
 * toggle — all in a single init/settle/deinit cycle to minimise
 * register-write overhead and serial output volume.
 * ==================================================================== */

void test_gpio_loopback_pa9_pb7(void)
{
    gpio_lb_init_pa9_pb7();

    /* HIGH */
    gpio_set_pin(GPIO_PORT_A, 9);
    gpio_lb_settle();
    TEST_ASSERT_EQUAL_MESSAGE(1, gpio_read_pin(GPIO_PORT_B, 7),
        "PA9=HIGH should read PB7=HIGH — check jumper PA9<->PB7");

    /* LOW */
    gpio_clear_pin(GPIO_PORT_A, 9);
    gpio_lb_settle();
    TEST_ASSERT_EQUAL_MESSAGE(0, gpio_read_pin(GPIO_PORT_B, 7),
        "PA9=LOW should read PB7=LOW — check jumper PA9<->PB7");

    /* Toggle HIGH → LOW */
    gpio_toggle_pin(GPIO_PORT_A, 9);
    gpio_lb_settle();
    TEST_ASSERT_EQUAL_MESSAGE(1, gpio_read_pin(GPIO_PORT_B, 7),
        "PA9 after toggle: PB7 should be HIGH");

    gpio_toggle_pin(GPIO_PORT_A, 9);
    gpio_lb_settle();
    TEST_ASSERT_EQUAL_MESSAGE(0, gpio_read_pin(GPIO_PORT_B, 7),
        "PA9 after second toggle: PB7 should be LOW");

    gpio_lb_deinit_pa9_pb7();
}

void test_gpio_loopback_pc6_pc7(void)
{
    gpio_lb_init_pc6_pc7();

    /* HIGH */
    gpio_set_pin(GPIO_PORT_C, 6);
    gpio_lb_settle();
    TEST_ASSERT_EQUAL_MESSAGE(1, gpio_read_pin(GPIO_PORT_C, 7),
        "PC6=HIGH should read PC7=HIGH — check jumper PC6<->PC7");

    /* LOW */
    gpio_clear_pin(GPIO_PORT_C, 6);
    gpio_lb_settle();
    TEST_ASSERT_EQUAL_MESSAGE(0, gpio_read_pin(GPIO_PORT_C, 7),
        "PC6=LOW should read PC7=LOW — check jumper PC6<->PC7");

    gpio_lb_deinit_pc6_pc7();
}

/* ====================================================================
 * EXTI interrupt tests — PA9 output triggers EXTI line 7 (port B, PB7)
 *
 * Strategy: use EXTI_MODE_INTERRUPT so that EXTI->PR is set on each
 * detected edge (the pending register is only updated in interrupt mode).
 * A minimal EXTI9_5_IRQHandler defined here overrides the Default_Handler
 * (infinite loop) weak alias: it increments a volatile counter and clears
 * the pending bit, then returns — allowing the test to observe how many
 * times the handler fired.
 *
 * Timeout guard: spin up to ~1 ms (100 000 cycles at 100 MHz) before
 * declaring a failure — long enough for any reasonable signal path but
 * short enough to avoid a multi-second hang on a disconnected jumper.
 * ==================================================================== */

/* Shared ISR counter — incremented by EXTI9_5_IRQHandler on each fire */
static volatile uint32_t g_exti9_5_count = 0;

/**
 * @brief Minimal EXTI9_5 handler: count the interrupt and clear pending.
 *
 * Overrides the Default_Handler weak alias in startup code.
 * Clears only line 7 (PB7) pending bit; other lines in the [5-9] group
 * are left untouched (none are armed in these tests).
 */
void EXTI9_5_IRQHandler(void)
{
    if (EXTI->PR & (1U << 7)) {
        EXTI->PR = (1U << 7);   /* write 1 to clear */
        g_exti9_5_count++;
    }
}

#define EXTI_LB_TIMEOUT_CYCLES  100000U

/**
 * @brief Spin until g_exti9_5_count exceeds prev_count, or timeout.
 * @return 1 if count increased, 0 on timeout.
 */
static int exti_lb_wait_count(uint32_t prev_count)
{
    uint32_t n = EXTI_LB_TIMEOUT_CYCLES;
    while (n--) {
        if (g_exti9_5_count != prev_count) return 1;
    }
    return 0;
}

void test_exti_both_edges_pb7(void)
{
    gpio_lb_init_pa9_pb7();
    gpio_clear_pin(GPIO_PORT_A, 9);
    gpio_lb_settle();

    int ret = exti_configure_gpio_interrupt(GPIO_PORT_B, 7,
                                            EXTI_TRIGGER_BOTH,
                                            EXTI_MODE_INTERRUPT);
    TEST_ASSERT_EQUAL_MESSAGE(0, ret,
        "exti_configure_gpio_interrupt(PB7, BOTH) failed");

    exti_clear_pending(7);
    uint32_t prev = g_exti9_5_count;

    /* Rising edge */
    gpio_set_pin(GPIO_PORT_A, 9);
    int fired_rising = exti_lb_wait_count(prev);
    prev = g_exti9_5_count;

    /* Falling edge */
    gpio_clear_pin(GPIO_PORT_A, 9);
    int fired_falling = exti_lb_wait_count(prev);

    exti_clear_pending(7);
    exti_disable_line(7);
    exti_set_interrupt_mask(7, 0);
    gpio_lb_deinit_pa9_pb7();

    TEST_ASSERT_MESSAGE(fired_rising,
        "EXTI BOTH: rising edge did not fire — check jumper PA9<->PB7");
    TEST_ASSERT_MESSAGE(fired_falling,
        "EXTI BOTH: falling edge did not fire — check jumper PA9<->PB7");
}

void test_exti_software_trigger_pb7(void)
{
    /* Software trigger: no loopback cable required.
     * Writes to EXTI->SWIER to generate a synthetic rising edge.
     * The EXTI9_5_IRQHandler defined above will fire and increment
     * g_exti9_5_count, confirming end-to-end EXTI machinery works.  */
    gpio_clock_enable(GPIO_PORT_B);
    gpio_configure_pin(GPIO_PORT_B, 7, GPIO_MODE_INPUT);

    /* Enable SYSCFG clock (needed by exti_configure_gpio_interrupt) */
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;

    /* Configure for rising edge, interrupt mode */
    int ret = exti_configure_gpio_interrupt(GPIO_PORT_B, 7,
                                            EXTI_TRIGGER_RISING,
                                            EXTI_MODE_INTERRUPT);
    TEST_ASSERT_EQUAL_MESSAGE(0, ret,
        "exti_configure_gpio_interrupt for software trigger test failed");

    exti_clear_pending(7);
    uint32_t prev = g_exti9_5_count;

    /* Fire software trigger */
    int sw_ret = exti_software_trigger(7);
    TEST_ASSERT_EQUAL_MESSAGE(0, sw_ret,
        "exti_software_trigger(7) returned error");

    /* Wait for ISR to fire */
    int fired = exti_lb_wait_count(prev);

    /* Cleanup */
    exti_clear_pending(7);
    exti_disable_line(7);
    exti_set_interrupt_mask(7, 0);
    gpio_configure_pin(GPIO_PORT_B, 7, GPIO_MODE_ANALOG);

    TEST_ASSERT_MESSAGE(fired,
        "EXTI software trigger: ISR did not fire");
}

/* ====================================================================
 * Flash hardware tests — Tier 6
 *
 * Uses sector 7 (0x08060000, 128 KB) as the scratch area.  Sector 7
 * is reserved for slot B in Phase 1.7 of the bootloader plan but
 * unused in Phase 1.5, which makes it the only sector that is both
 * (a) inside the on-chip flash and (b) not currently mapped to
 * anything the running firmware needs.
 *
 * History note (2026-05-30): this used to point at sector 4
 * (`FLASH_TEST_SECTOR=4`, `FLASH_TEST_BASE_ADDR=0x08010000`), which
 * happens to be the start of slot A under the Phase 1.5 bootloader
 * skeleton.  Running `run_all_tests` on a slot-A image would erase
 * the running image; the chip stayed alive long enough to print PASS
 * (I-cache + prefetch) but the next reset dropped into
 * `bootloader_halt()` with `mdw 0x08010000 4 == FFFFFFFF`.  The fix
 * also moved the affected tests to a sector the firmware does not
 * occupy and added an active-image guard
 * (`test_flash_scratch_sector_is_safe`) that fails loudly if a future
 * layout change moves the running image into the scratch sector.
 * See docs/wiki/plans/001-bootloader/spi1-dma-fault-investigation.md
 * for the full chain.
 *
 * Slot B (Phase 1.7) will reclaim sector 7.  When that happens, this
 * test must move with it: re-run host tests + a HIL pass and update
 * the constants below alongside the slot-B linker change.
 *
 * Wear-levelling strategy: a SINGLE erase per test run. The first test
 * erases + verifies, subsequent tests write to different offsets within
 * the already-erased sector. This gives ~10K CI runs before the sector
 * wears out (STM32F411 flash endurance spec).
 * ==================================================================== */

#define FLASH_TEST_SECTOR     7U
#define FLASH_TEST_BASE_ADDR  0x08060000U

/* Provided by linker/app_ls.ld for slot-relocated app builds; the
 * bootloader build leaves it undefined and we never reach the flash
 * tests from there, so the weak attribute keeps the link clean. */
extern uint32_t _app_vector_base __attribute__((weak));

void test_flash_scratch_sector_is_safe(void)
{
    /* Compile-time sanity: the constants in this file agree. */
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        flash_get_sector_address(FLASH_TEST_SECTOR),
        FLASH_TEST_BASE_ADDR,
        "FLASH_TEST_BASE_ADDR does not match flash_get_sector_address(FLASH_TEST_SECTOR)");

    /* Runtime guard: the scratch sector must not contain the running
     * image's vector table.  Without this, a future linker / slot-base
     * change could re-introduce the slot-A self-erase failure mode
     * silently (the test would still pass while quietly bricking slot A
     * on the next reset). */
    uintptr_t vbase = (uintptr_t)&_app_vector_base;
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0u, vbase,
        "_app_vector_base is 0 -- this test is only valid for slot-A app builds");

    uint8_t app_sector = 0xFFu;
    err_t rc = flash_sector_for_address((uint32_t)vbase, &app_sector);
    TEST_ASSERT_EQUAL_MESSAGE(ERR_OK, rc,
        "_app_vector_base is outside on-chip flash");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(FLASH_TEST_SECTOR, app_sector,
        "FLASH_TEST_SECTOR equals the running app's sector -- erasing it would brick the slot");
}

void test_flash_erase_scratch_sector(void)
{
    err_t ret = flash_unlock();
    TEST_ASSERT_EQUAL_MESSAGE(ERR_OK, ret, "flash_unlock() failed");

    ret = flash_erase_sector(FLASH_TEST_SECTOR);
    flash_lock();
    TEST_ASSERT_EQUAL_MESSAGE(ERR_OK, ret, "flash_erase_sector() failed");

    /* Verify erased state: first 32 bytes should all be 0xFF */
    uint8_t buf[32];
    flash_read_bytes(FLASH_TEST_BASE_ADDR, buf, sizeof(buf));
    for (uint32_t i = 0; i < sizeof(buf); i++) {
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFF, buf[i],
            "Erased flash byte not 0xFF");
    }
}

void test_flash_write_word_readback(void)
{
    /* Write to offset 0x00 and 0x04 (already erased by previous test) */
    flash_unlock();

    err_t ret = flash_write_word(FLASH_TEST_BASE_ADDR, 0xDEADBEEFU);
    TEST_ASSERT_EQUAL_MESSAGE(ERR_OK, ret, "flash_write_word() failed");

    ret = flash_write_word(FLASH_TEST_BASE_ADDR + 4U, 0xCAFEBABEU);
    TEST_ASSERT_EQUAL_MESSAGE(ERR_OK, ret, "flash_write_word(+4) failed");

    flash_lock();

    uint32_t val0, val1;
    flash_read_word(FLASH_TEST_BASE_ADDR, &val0);
    flash_read_word(FLASH_TEST_BASE_ADDR + 4U, &val1);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xDEADBEEFU, val0,
        "Word readback mismatch at base");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xCAFEBABEU, val1,
        "Word readback mismatch at base+4");
}

void test_flash_write_bytes_readback(void)
{
    /* Write to offset 0x100 to avoid clobbering the word test above */
    const uint32_t addr = FLASH_TEST_BASE_ADDR + 0x100U;

    static const uint8_t pattern[] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0, 0x55, 0xAA
    };

    flash_unlock();
    err_t ret = flash_write_bytes(addr, pattern, sizeof(pattern));
    flash_lock();
    TEST_ASSERT_EQUAL_MESSAGE(ERR_OK, ret, "flash_write_bytes() failed");

    uint8_t readback[16];
    flash_read_bytes(addr, readback, sizeof(readback));
    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(pattern, readback, sizeof(pattern),
        "Byte pattern readback mismatch");
}

void test_flash_sector_info(void)
{
    TEST_ASSERT_EQUAL_HEX32(FLASH_TEST_BASE_ADDR,
                            flash_get_sector_address(FLASH_TEST_SECTOR));
    TEST_ASSERT_EQUAL_UINT32(128U * 1024U,
                             flash_get_sector_size(FLASH_TEST_SECTOR));
}

/* ====================================================================
 * CRC hardware tests — Tier 7
 *
 * The STM32F411 CRC peripheral uses polynomial 0x04C11DB7 (MPEG-2),
 * initial value 0xFFFFFFFF, no output XOR, no bit reversal.
 * Pure internal peripheral — no external wiring required.
 * ==================================================================== */

/* Software CRC-32/MPEG-2 reference for cross-validation */
static uint32_t sw_crc32_mpeg2_word(const uint32_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 32; bit++) {
            if (crc & 0x80000000U) {
                crc = (crc << 1) ^ 0x04C11DB7U;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}

void test_crc_hw_known_vector(void)
{
    static const uint32_t vec[] = {0x00000001, 0x00000002, 0x00000003,
                                   0x04050607, 0x08090A0B, 0x0C0D0E0F};
    uint32_t expected = sw_crc32_mpeg2_word(vec, 6);

    crc_init();
    crc_reset();
    uint32_t hw_result = crc_accumulate(vec, 6);

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(expected, hw_result,
        "HW CRC does not match software reference for known vector");
}

void test_crc_hw_reset_restores_init(void)
{
    static const uint32_t data[] = {0xDEADBEEF, 0xCAFEBABE};
    crc_init();
    crc_accumulate(data, 2);

    crc_reset();
    uint32_t result = crc_get_result();
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xFFFFFFFFU, result,
        "CRC reset did not restore accumulator to 0xFFFFFFFF");
}

void test_crc_hw_accumulate_matches_sequential(void)
{
    static const uint32_t data[] = {0x11223344, 0x55667788, 0x99AABBCC, 0xDDEEFF00};

    crc_init();
    crc_reset();
    uint32_t bulk = crc_accumulate(data, 4);

    crc_reset();
    for (uint32_t i = 0; i < 4; i++) {
        crc_accumulate(&data[i], 1);
    }
    uint32_t sequential = crc_get_result();

    TEST_ASSERT_EQUAL_HEX32_MESSAGE(bulk, sequential,
        "Bulk accumulate != sequential word-by-word");
}

void test_crc_hw_flash_region(void)
{
    crc_init();
    crc_reset();
    uint32_t result1 = crc_accumulate((const uint32_t *)0x08000000U, 256);

    TEST_ASSERT_NOT_EQUAL_HEX32_MESSAGE(0U, result1,
        "CRC of flash region should not be zero");
    TEST_ASSERT_NOT_EQUAL_HEX32_MESSAGE(0xFFFFFFFFU, result1,
        "CRC of flash region should not be init value");

    /* Determinism: same input must give same output */
    crc_reset();
    uint32_t result2 = crc_accumulate((const uint32_t *)0x08000000U, 256);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(result1, result2,
        "CRC of same flash region not deterministic");
}

void test_crc_hw_performance(void)
{
    static uint32_t perf_buf[1024];
    for (uint32_t i = 0; i < 1024; i++) {
        perf_buf[i] = i * 0x01010101U;
    }

    /* Enable DWT cycle counter */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

    crc_init();
    crc_reset();

    uint32_t start = DWT->CYCCNT;
    crc_accumulate(perf_buf, 1024);
    uint32_t elapsed = DWT->CYCCNT - start;

    /* CRC peripheral takes 4 AHB cycles per word. With loop overhead
     * (load, store, branch), expect ~6 cycles/word = ~6144 for 1024 words.
     * Allow generous margin for pipeline and flash wait states. */
    TEST_ASSERT_UINT32_WITHIN_MESSAGE(
        3000U, 6200U, elapsed,
        "CRC of 1024 words took too many cycles");

    printf("  [perf] CRC 1024 words: %lu cycles (%.1f cycles/word)\n",
           (unsigned long)elapsed, (float)elapsed / 1024.0f);
}

/* ====================================================================
 * Stop mode test — Tier 8
 *
 * Uses EXTI software trigger on line 7 (PB7) to wake immediately from
 * Stop mode without external wiring or hang risk. After wake, the PLL
 * is restored to 100 MHz and systick re-initialised.
 * ==================================================================== */

void test_stop_mode_enter_and_wake(void)
{
    /* Configure PB7 as input, enable EXTI line 7 rising edge */
    gpio_clock_enable(GPIO_PORT_B);
    gpio_configure_pin(GPIO_PORT_B, 7, GPIO_MODE_INPUT);
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;

    int ret = exti_configure_gpio_interrupt(GPIO_PORT_B, 7,
                                            EXTI_TRIGGER_RISING,
                                            EXTI_MODE_INTERRUPT);
    TEST_ASSERT_EQUAL_MESSAGE(0, ret, "EXTI config for stop wake failed");

    /* Set EXTI pending via software trigger so WFI wakes immediately */
    exti_software_trigger(7);

    /* Enter Stop mode — wakes immediately due to pending EXTI */
    enter_stop_mode();

    /* Restore PLL to 100 MHz (Stop mode reverts to HSI) */
    rcc_init(RCC_CLK_SRC_HSI, 100000000U);
    systick_init();

    /* Verify clock restored */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(100000000U, rcc_get_sysclk(),
        "SYSCLK not restored to 100 MHz after Stop mode wake");

    /* Cleanup EXTI */
    exti_clear_pending(7);
    exti_disable_line(7);
    exti_set_interrupt_mask(7, 0);
    gpio_configure_pin(GPIO_PORT_B, 7, GPIO_MODE_ANALOG);
}

/* ====================================================================
 * Software BPSK modem — Tier 9 (Plan 002 B0.3)
 *
 * Runs the self-contained PRBS -> BPSK -> AWGN -> slice -> BER chain on the
 * Cortex-M4F and reports two metrics: the measured bit-error rate (in parts
 * per million) and the DWT cycle count for the run (from which cycles/bit is
 * derived).  No external wiring — the channel is software AWGN.
 *
 * Correctness gate (computed on-device): the measured BER must land within a
 * factor of two of the closed-form BPSK theory, AND cycles/bit must be under a
 * generous budget.  The factor-of-two band is deliberately loose: with a fixed
 * seed the run is deterministic, but the band stays robust to toolchain/libm
 * drift while still catching a broken chain (BER ~0 or ~0.5).  The tight
 * performance gate is the baseline JSON (cycles +/- tolerance).
 * ==================================================================== */

#define MODEM_BER_SEED            1u
#define MODEM_BER_SNR_DB          6.0f
/*
 * 100000 bits at theory BER ~2.4e-3 gives ~239 expected errors, enough
 * statistical margin for the factor-of-two band below; shrinking this would
 * make the band flaky.  cyc/bit is dominated by the AWGN Box-Muller (libm
 * sqrtf/logf/sinf/cosf), not the BPSK map/slice — hence the loose budget.
 */
#define MODEM_BER_NBITS           100000u
/*
 * Measured ~1039 cyc/bit on the F411 @ 100 MHz (Box-Muller sqrtf/logf/sinf/
 * cosf per noise sample dominates).  1500 leaves ~45% headroom as a coarse
 * "did something blow up" guard; the tight performance gate is the baseline
 * JSON cycles +/- tolerance, not this number.
 */
#define MODEM_CYC_PER_BIT_BUDGET  1500u

/*
 * Block buffers for the staged chain (see modem_run_chain in
 * apps/dsp/modem_sim.c — the test mirrors it so the CI breakdown matches the
 * interactive app).  4 KB of .bss (1+2+1), only present in HIL builds.
 */
#define MODEM_BER_BLOCK 1024u
static uint8_t modem_tx_block[MODEM_BER_BLOCK];
static q15_t   modem_sym_block[MODEM_BER_BLOCK];
static uint8_t modem_rx_block[MODEM_BER_BLOCK];

void test_modem_bpsk_ber_awgn(void)
{
    prbs_t      tx;
    awgn_prng_t rng;

    prbs_init(&tx, PRBS9, MODEM_BER_SEED);
    awgn_prng_seed(&rng, MODEM_BER_SEED);

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /*
     * Five separately-timed stages, accumulated across blocks.  Errors are
     * counted by comparing the sliced rx bits against the generated tx bits,
     * so the check stage measures a real comparison rather than a second PRBS
     * regeneration.  Mirrors apps/dsp/modem_sim.c modem_run_chain().
     */
    uint32_t gen_cyc = 0, mod_cyc = 0, chan_cyc = 0, demod_cyc = 0, check_cyc = 0;
    uint64_t errors = 0;

    uint32_t remaining = MODEM_BER_NBITS;
    while (remaining > 0u) {
        uint32_t n = (remaining < MODEM_BER_BLOCK) ? remaining : MODEM_BER_BLOCK;

        uint32_t t0 = DWT->CYCCNT;
        prbs_next_bits(&tx, modem_tx_block, n);
        uint32_t t1 = DWT->CYCCNT;
        bpsk_map_block(modem_tx_block, modem_sym_block, n);
        uint32_t t2 = DWT->CYCCNT;
        channel_awgn_apply(modem_sym_block, n, MODEM_BER_SNR_DB, &rng);
        uint32_t t3 = DWT->CYCCNT;
        bpsk_slice_block(modem_sym_block, modem_rx_block, n);
        uint32_t t4 = DWT->CYCCNT;
        for (uint32_t i = 0; i < n; i++) {
            if (modem_rx_block[i] != modem_tx_block[i]) {
                errors++;
            }
        }
        uint32_t t5 = DWT->CYCCNT;

        gen_cyc   += t1 - t0;
        mod_cyc   += t2 - t1;
        chan_cyc  += t3 - t2;
        demod_cyc += t4 - t3;
        check_cyc += t5 - t4;
        remaining -= n;
    }

    uint32_t cycles      = gen_cyc + mod_cyc + chan_cyc + demod_cyc + check_cyc;
    uint64_t total       = MODEM_BER_NBITS;
    uint32_t ber_ppm     = (uint32_t)((errors * 1000000ull) / total);
    double   theory      = channel_awgn_theory_ber(MODEM_BER_SNR_DB);
    uint32_t theory_ppm  = (uint32_t)(theory * 1.0e6 + 0.5);
    uint32_t cyc_per_bit = (uint32_t)(cycles / total);

    /* Factor-of-two correctness band around theory. */
    int ber_ok = (theory_ppm > 0u) &&
                 (ber_ppm >= theory_ppm / 2u) && (ber_ppm <= theory_ppm * 2u);
    int cyc_ok = (cyc_per_bit <= MODEM_CYC_PER_BIT_BUDGET);
    int pass   = ber_ok && cyc_ok;

    /*
     * Emit the machine-parseable lines BEFORE the asserts: Unity longjmps out
     * of the test on the first failure, so the metrics must already be printed
     * for the runner to record them even on a fail.  The main line is gated
     * (ber_ppm + total cycles); the five per-stage lines are report-only
     * (null baselines) so the breakdown shows up in CI / JUnit without gating.
     * Per-stage value is cyc/1000-bits for sub-cyc/bit resolution.
     *
     * Flush after every line: six TEST: lines back-to-back overrun the
     * printf_dma buffer, which truncated/merged the demod+check lines in the
     * serial capture otherwise.
     */
    TEST_OUTPUT_RESULT("modem_bpsk_ber_snr6", pass, cycles, "ber_ppm", ber_ppm);
    printf_dma_flush();
    TEST_OUTPUT_RESULT("modem_cyc_gen",   1, gen_cyc,   "cyc_per_kbit",
                       (uint32_t)((uint64_t)gen_cyc   * 1000u / total));
    printf_dma_flush();
    TEST_OUTPUT_RESULT("modem_cyc_mod",   1, mod_cyc,   "cyc_per_kbit",
                       (uint32_t)((uint64_t)mod_cyc   * 1000u / total));
    printf_dma_flush();
    TEST_OUTPUT_RESULT("modem_cyc_chan",  1, chan_cyc,  "cyc_per_kbit",
                       (uint32_t)((uint64_t)chan_cyc  * 1000u / total));
    printf_dma_flush();
    TEST_OUTPUT_RESULT("modem_cyc_demod", 1, demod_cyc, "cyc_per_kbit",
                       (uint32_t)((uint64_t)demod_cyc * 1000u / total));
    printf_dma_flush();
    TEST_OUTPUT_RESULT("modem_cyc_check", 1, check_cyc, "cyc_per_kbit",
                       (uint32_t)((uint64_t)check_cyc * 1000u / total));
    printf_dma_flush();

    printf("  [modem] BER=%.3e theory=%.3e cyc/bit=%lu\n",
           (double)ber_ppm / 1.0e6, theory, (unsigned long)cyc_per_bit);
    /* Per-stage cost in cyc per 1000 bits: the non-channel stages are well
     * under 1 cyc/bit, so kbit resolution keeps them from rounding to 0. */
    printf("  [modem] cyc/kbit gen=%lu mod=%lu chan=%lu demod=%lu check=%lu\n",
           (unsigned long)((uint64_t)gen_cyc   * 1000u / total),
           (unsigned long)((uint64_t)mod_cyc   * 1000u / total),
           (unsigned long)((uint64_t)chan_cyc  * 1000u / total),
           (unsigned long)((uint64_t)demod_cyc * 1000u / total),
           (unsigned long)((uint64_t)check_cyc * 1000u / total));
    printf_dma_flush();

    TEST_ASSERT_TRUE_MESSAGE(ber_ok, "BPSK BER outside factor-2 band of theory");
    TEST_ASSERT_TRUE_MESSAGE(cyc_ok, "BPSK modem cyc/bit over budget");
}

/* ====================================================================
 * Software BPSK modem with RRC pulse shaping — Tier 9b (Plan 002 B0.4b, #207)
 *
 * Same self-contained chain as test_modem_bpsk_ber_awgn, but the symbols are
 * RRC-pulse-shaped to oversampled waveforms, run through AWGN at the sample
 * rate, matched-filtered, and decimated at the symbol instants before slicing.
 * The matched filter is information-lossless, so with unit-energy taps the BER
 * still tracks the unshaped BPSK theory curve — that equivalence is the point
 * of the test.  cyc/bit is much higher than the unshaped path (two FIR passes
 * over SPS x the samples), so it carries its own budget.
 *
 * Mirrors apps/dsp/modem_sim.c modem_run_chain_shaped() (PRBS9, seed 1, 6 dB).
 * ==================================================================== */

#define MODEM_SHAPE_SPS           4u
#define MODEM_SHAPE_BETA          0.35f
#define MODEM_SHAPE_SPAN          8u
/*
 * Shaped cyc/bit is dominated by the two RRC FIR passes (33 taps over SPS x the
 * samples) plus the AWGN at sample rate.  CI measured ~5483 cyc/bit; 8000 keeps
 * a coarse "did something blow up" guard with clear headroom above the baseline
 * JSON's +15% upper band (~6300).  The tight gate is the baseline cycles +/-
 * tolerance, not this number — same split as the unshaped budget.
 */
#define MODEM_SHAPE_CYC_PER_BIT_BUDGET 8000u

static q15_t modem_shape_samp[MODEM_BER_BLOCK * MODEM_SHAPE_SPS];
static rrc_t modem_shape_tx;
static rrc_t modem_shape_rx;

void test_modem_bpsk_ber_awgn_shaped(void)
{
    prbs_t       tx;
    prbs_check_t chk;
    awgn_prng_t  rng;

    prbs_init(&tx, PRBS9, MODEM_BER_SEED);
    prbs_check_init(&chk, PRBS9, MODEM_BER_SEED);
    awgn_prng_seed(&rng, MODEM_BER_SEED);

    rrc_design(&modem_shape_tx, MODEM_SHAPE_BETA, MODEM_SHAPE_SPS, MODEM_SHAPE_SPAN);
    rrc_design(&modem_shape_rx, MODEM_SHAPE_BETA, MODEM_SHAPE_SPS, MODEM_SHAPE_SPAN);

    const uint32_t sps = MODEM_SHAPE_SPS;
    const size_t   delay_samples = rrc_chain_delay(&modem_shape_tx);
    const size_t   tail_syms = delay_samples / sps + 1u;
    const uint32_t total_syms = MODEM_BER_NBITS + (uint32_t)tail_syms;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    uint32_t gen_cyc = 0, mod_cyc = 0, shape_cyc = 0, chan_cyc = 0,
             match_cyc = 0, demod_cyc = 0, check_cyc = 0;
    uint64_t errors = 0;

    size_t   sample_base = 0;
    size_t   next_peak   = delay_samples;
    uint32_t produced    = 0;
    uint32_t sym_done    = 0;

    while (sym_done < total_syms) {
        uint32_t n = total_syms - sym_done;
        if (n > MODEM_BER_BLOCK) {
            n = MODEM_BER_BLOCK;
        }
        uint32_t payload_n = 0;
        if (sym_done < MODEM_BER_NBITS) {
            payload_n = MODEM_BER_NBITS - sym_done;
            if (payload_n > n) {
                payload_n = n;
            }
        }

        uint32_t t0 = DWT->CYCCNT;
        if (payload_n > 0u) {
            prbs_next_bits(&tx, modem_tx_block, payload_n);
        }
        uint32_t t1 = DWT->CYCCNT;
        for (uint32_t i = 0; i < payload_n; i++) {
            modem_sym_block[i] = bpsk_map(modem_tx_block[i]);
        }
        for (uint32_t i = payload_n; i < n; i++) {
            modem_sym_block[i] = 0;
        }
        uint32_t t2 = DWT->CYCCNT;
        rrc_tx_shape(&modem_shape_tx, modem_sym_block, n, modem_shape_samp);
        uint32_t t3 = DWT->CYCCNT;
        channel_awgn_apply(modem_shape_samp, (size_t)n * sps, MODEM_BER_SNR_DB, &rng);
        uint32_t t4 = DWT->CYCCNT;
        rrc_rx_match(&modem_shape_rx, modem_shape_samp, (size_t)n * sps, modem_shape_samp);
        uint32_t t5 = DWT->CYCCNT;
        uint32_t dec_n = 0;
        for (uint32_t p = 0; p < n * sps; p++) {
            if (sample_base + p == next_peak && (produced + dec_n) < MODEM_BER_NBITS) {
                modem_rx_block[dec_n++] = bpsk_slice(modem_shape_samp[p]);
                next_peak += sps;
            }
        }
        uint32_t t6 = DWT->CYCCNT;
        for (uint32_t i = 0; i < dec_n; i++) {
            if (!prbs_check_bit(&chk, modem_rx_block[i])) {
                errors++;
            }
        }
        uint32_t t7 = DWT->CYCCNT;

        gen_cyc   += t1 - t0;
        mod_cyc   += t2 - t1;
        shape_cyc += t3 - t2;
        chan_cyc  += t4 - t3;
        match_cyc += t5 - t4;
        demod_cyc += t6 - t5;
        check_cyc += t7 - t6;

        produced    += dec_n;
        sample_base += (size_t)n * sps;
        sym_done    += n;
    }

    uint32_t cycles      = gen_cyc + mod_cyc + shape_cyc + chan_cyc +
                           match_cyc + demod_cyc + check_cyc;
    uint64_t total       = produced;   /* compared symbols (== NBITS once flushed) */
    uint32_t ber_ppm     = (total > 0u) ? (uint32_t)((errors * 1000000ull) / total) : 0u;
    double   theory      = channel_awgn_theory_ber(MODEM_BER_SNR_DB);
    uint32_t theory_ppm  = (uint32_t)(theory * 1.0e6 + 0.5);
    uint32_t cyc_per_bit = (total > 0u) ? (uint32_t)(cycles / total) : 0u;

    int ber_ok = (theory_ppm > 0u) &&
                 (ber_ppm >= theory_ppm / 2u) && (ber_ppm <= theory_ppm * 2u);
    int cyc_ok = (cyc_per_bit <= MODEM_SHAPE_CYC_PER_BIT_BUDGET);
    int pass   = ber_ok && cyc_ok;

    /* Gated main line + report-only per-stage breakdown, flushed individually
     * (see the unshaped test for why).  Per-stage value is cyc/1000-bits. */
    TEST_OUTPUT_RESULT("modem_shaped_ber_snr6", pass, cycles, "ber_ppm", ber_ppm);
    printf_dma_flush();
    TEST_OUTPUT_RESULT("modem_shaped_cyc_gen",   1, gen_cyc,   "cyc_per_kbit",
                       (uint32_t)((uint64_t)gen_cyc   * 1000u / total));
    printf_dma_flush();
    TEST_OUTPUT_RESULT("modem_shaped_cyc_mod",   1, mod_cyc,   "cyc_per_kbit",
                       (uint32_t)((uint64_t)mod_cyc   * 1000u / total));
    printf_dma_flush();
    TEST_OUTPUT_RESULT("modem_shaped_cyc_shape", 1, shape_cyc, "cyc_per_kbit",
                       (uint32_t)((uint64_t)shape_cyc * 1000u / total));
    printf_dma_flush();
    TEST_OUTPUT_RESULT("modem_shaped_cyc_chan",  1, chan_cyc,  "cyc_per_kbit",
                       (uint32_t)((uint64_t)chan_cyc  * 1000u / total));
    printf_dma_flush();
    TEST_OUTPUT_RESULT("modem_shaped_cyc_match", 1, match_cyc, "cyc_per_kbit",
                       (uint32_t)((uint64_t)match_cyc * 1000u / total));
    printf_dma_flush();
    TEST_OUTPUT_RESULT("modem_shaped_cyc_demod", 1, demod_cyc, "cyc_per_kbit",
                       (uint32_t)((uint64_t)demod_cyc * 1000u / total));
    printf_dma_flush();
    TEST_OUTPUT_RESULT("modem_shaped_cyc_check", 1, check_cyc, "cyc_per_kbit",
                       (uint32_t)((uint64_t)check_cyc * 1000u / total));
    printf_dma_flush();

    printf("  [modem/rrc] BER=%.3e theory=%.3e cyc/bit=%lu bits=%lu\n",
           (double)ber_ppm / 1.0e6, theory, (unsigned long)cyc_per_bit,
           (unsigned long)total);
    printf_dma_flush();

    TEST_ASSERT_TRUE_MESSAGE(ber_ok, "Shaped BPSK BER outside factor-2 band of theory");
    TEST_ASSERT_TRUE_MESSAGE(cyc_ok, "Shaped BPSK modem cyc/bit over budget");
}

/* ====================================================================
 * Software BPSK modem with impairments + RX recovery — Tier 9c
 * (Plan 002 B0.5, #197)
 *
 * The headline B0.5 validation on real hardware: a Barker-13-framed PRBS frame
 * is RRC-shaped, passed through the impairment channel (fractional timing + CFO
 * + static phase) and complex-baseband AWGN, then run through the full receiver
 *
 *   matched filter -> AGC -> M&M timing -> Costas phase -> Barker frame/polarity
 *   -> slice -> compare
 *
 * Mirrors apps/dsp/modem_sim.c modem_run_chain_sync() and the host twin
 * tests/lib/sync/test_recover_e2e.c (PRBS9, seed 1, combined timing 0.4 + CFO
 * 2e-4 + phase ~33 deg). Asserts: (a) the preamble locks, (b) post-sync BER is
 * within a bounded multiple of the ideal-sync theory, and (c) cyc/bit is under a
 * coarse budget. The recovery loops are sequential, so this runs symbol-by-symbol
 * over a smaller frame than the bulk BER tiers (host model: lock @sym 20,
 * 6 dB BER ~2.88e-3 vs theory 2.39e-3).
 * ==================================================================== */

#define MODEM_SYNC_BITS           8000u
#define MODEM_SYNC_SNR_DB         6.0f
#define MODEM_SYNC_TIMING_MU_F    0.4f
#define MODEM_SYNC_CFO_CYCLES     2.0e-4f
#define MODEM_SYNC_PHASE_RAD      0.5759587f   /* ~33 deg */
/* Bounded degradation vs ideal-sync: recovery + linear-interp penalty keep the
 * combined-offset BER within ~4x theory (the host e2e test uses the same bound). */
#define MODEM_SYNC_BER_BOUND_MULT 4.0
/* Sequential per-symbol RX through four loops + two matched filters is far
 * heavier per bit than the bulk paths; this coarse guard just catches a blow-up,
 * the baseline JSON carries the tight gate. */
#define MODEM_SYNC_CYC_PER_BIT_BUDGET 60000u

static uint8_t  modem_sync_payload[MODEM_SYNC_BITS];
static uint8_t  modem_sync_rec[MODEM_SYNC_BITS + 64];
static rrc_t    modem_sync_tx;
static rrc_t    modem_sync_rxi;
static rrc_t    modem_sync_rxq;

void test_modem_bpsk_recover_sync(void)
{
    const uint32_t sps = MODEM_SHAPE_SPS;

    rrc_design(&modem_sync_tx,  MODEM_SHAPE_BETA, (uint8_t)sps, MODEM_SHAPE_SPAN);
    rrc_design(&modem_sync_rxi, MODEM_SHAPE_BETA, (uint8_t)sps, MODEM_SHAPE_SPAN);
    rrc_design(&modem_sync_rxq, MODEM_SHAPE_BETA, (uint8_t)sps, MODEM_SHAPE_SPAN);

    channel_impair_cfg_t cfg = {
        nco_phase_from_cycles(MODEM_SYNC_CFO_CYCLES),
        nco_phase_from_rad(MODEM_SYNC_PHASE_RAD),
        (q15_t)(MODEM_SYNC_TIMING_MU_F * 32768.0f),
    };
    channel_impair_state_t imp;
    channel_impair_init(&imp, &cfg);

    awgn_prng_t rng;
    awgn_prng_seed(&rng, MODEM_BER_SEED);

    agc_t agc;       agc_init(&agc, 0.7f, 0.005f, 1.0f);
    timing_mm_t mm;  timing_mm_init(&mm, (uint8_t)sps, 0.004f);
    costas_t cos;    costas_init(&cos, 0.02f, 0.0005f);
    barker_t bar;
    double  esym = 0.7 * 32768.0;
    int64_t thr  = (int64_t)((0.55 * 13.0 * esym) * (0.55 * 13.0 * esym));
    barker_init(&bar, thr);

    prbs_t txbits;
    prbs_init(&txbits, PRBS9, MODEM_BER_SEED);
    for (uint32_t i = 0; i < MODEM_SYNC_BITS; i++) {
        modem_sync_payload[i] = prbs_next_bit(&txbits);
    }

    const uint32_t nsyms = BARKER13_LEN + MODEM_SYNC_BITS;
    int      locked = 0, polarity = 1;
    int32_t  lock_sym = -1;
    uint32_t nrec = 0;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    uint32_t t_start = DWT->CYCCNT;

    q15_t  txsamp[MODEM_SHAPE_SPS];
    cq15_t imp_out[MODEM_SHAPE_SPS];

    for (uint32_t k = 0; k < nsyms; k++) {
        q15_t sym;
        if (k < (uint32_t)BARKER13_LEN) {
            sym = BARKER13[k] > 0 ? bpsk_map(1) : bpsk_map(0);
        } else {
            sym = bpsk_map(modem_sync_payload[k - BARKER13_LEN]);
        }

        rrc_tx_shape(&modem_sync_tx, &sym, 1, txsamp);
        channel_impair_apply(&imp, &cfg, txsamp, imp_out, sps);
        channel_awgn_apply_cq15(imp_out, sps, MODEM_SYNC_SNR_DB, &rng);

        for (uint32_t p = 0; p < sps; p++) {
            cq15_t mf = cq15_make(rrc_push(&modem_sync_rxi, imp_out[p].re),
                                  rrc_push(&modem_sync_rxq, imp_out[p].im));
            cq15_t g = agc_apply(&agc, mf);
            cq15_t sym_out;
            if (!timing_mm_push(&mm, g, &sym_out)) {
                continue;
            }
            cq15_t y = costas_step(&cos, sym_out);
            if (!locked) {
                int32_t corr_re;
                if (barker_push(&bar, y, &corr_re, NULL)) {
                    locked   = 1;
                    lock_sym = (int32_t)k;
                    polarity = (corr_re >= 0) ? 1 : -1;
                }
            } else if (nrec < (uint32_t)(sizeof(modem_sync_rec))) {
                q15_t corrected = (polarity >= 0) ? y.re : (q15_t)(-y.re);
                modem_sync_rec[nrec++] = bpsk_slice(corrected);
            }
        }
    }
    uint32_t cycles = DWT->CYCCNT - t_start;

    /* Align over the matched-filter group delay, count errors at best alignment. */
    uint64_t best_err = nrec;
    uint64_t total    = (nrec > 0u) ? 1u : 0u;
    for (uint32_t D = 0; D < 25u && D < nrec; D++) {
        uint64_t err = 0, cnt = 0;
        for (uint32_t i = 0; i + D < nrec && i < MODEM_SYNC_BITS; i++) {
            if (modem_sync_rec[i + D] != modem_sync_payload[i]) {
                err++;
            }
            cnt++;
        }
        if (cnt > 0u && err < best_err) {
            best_err = err;
            total    = cnt;
        }
    }

    double   ber         = (total > 0u) ? (double)best_err / (double)total : 1.0;
    uint32_t ber_ppm     = (uint32_t)(ber * 1.0e6 + 0.5);
    double   theory      = channel_awgn_theory_ber(MODEM_SYNC_SNR_DB);
    uint32_t cyc_per_bit = (total > 0u) ? (uint32_t)((uint64_t)cycles / total) : 0u;

    int lock_ok = locked && (total > 0u);
    int ber_ok  = lock_ok && (ber <= MODEM_SYNC_BER_BOUND_MULT * theory + 1.0e-3);
    int cyc_ok  = (cyc_per_bit <= MODEM_SYNC_CYC_PER_BIT_BUDGET);
    int pass    = lock_ok && ber_ok && cyc_ok;

    TEST_OUTPUT_RESULT("modem_sync_recover_snr6", pass, cycles, "ber_ppm", ber_ppm);
    printf_dma_flush();
    /* Report-only: frame-lock symbol index (proves the preamble was found). */
    TEST_OUTPUT_RESULT("modem_sync_lock_sym", lock_ok, (uint32_t)(lock_sym + 1),
                       "locked", locked ? 1u : 0u);
    printf_dma_flush();

    printf("  [modem/sync] lock=%s@%ld BER=%.3e theory=%.3e (<=%.0fx) cyc/bit=%lu bits=%lu\n",
           locked ? "yes" : "NO", (long)lock_sym, ber, theory,
           MODEM_SYNC_BER_BOUND_MULT, (unsigned long)cyc_per_bit,
           (unsigned long)total);
    printf_dma_flush();

    TEST_ASSERT_TRUE_MESSAGE(lock_ok, "Barker frame sync did not lock");
    TEST_ASSERT_TRUE_MESSAGE(ber_ok, "Recovered BER outside bound vs ideal-sync theory");
    TEST_ASSERT_TRUE_MESSAGE(cyc_ok, "Sync recovery cyc/bit over budget");
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
     * Tier 4: SysTick tests
     *   systick_init() is called by main() at startup, so the counter
     *   is already running when these tests execute.
     * ---------------------------------------------------------- */
    printf("\n--- Tier 4: SysTick tests ---\n");
    printf_dma_flush();

    RUN_TEST(test_systick_get_ms_increments);
    RUN_TEST(test_systick_elapsed_since);
    RUN_TEST(test_systick_delay_ms_accuracy);

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
    printf("\n--- Tier 5: GPIO loopback ---\n");
    printf_dma_flush();

    RUN_TEST(test_gpio_loopback_pa9_pb7);   /* HIGH, LOW, toggle — PA9<->PB7 */
    RUN_TEST(test_gpio_loopback_pc6_pc7);   /* HIGH, LOW         — PC6<->PC7 */

    /* ----------------------------------------------------------
     * Tier 5: EXTI interrupt tests
     *   PA9 (output) wired to PB7 (input, EXTI line 7 port B)
     *   Both edges tested in one function; software trigger separate.
     * ---------------------------------------------------------- */
    printf("\n--- Tier 5: EXTI loopback (PA9->PB7, line 7) ---\n");
    printf_dma_flush();

    RUN_TEST(test_exti_both_edges_pb7);
    RUN_TEST(test_exti_software_trigger_pb7);

    /* ----------------------------------------------------------
     * Tier 6: Flash hardware tests
     *   Single erase of sector 1, then write/read at different offsets.
     *   1 erase cycle per CI run → ~10K runs before wear-out.
     *   No external wiring required.
     * ---------------------------------------------------------- */
    printf("\n--- Tier 6: Flash erase/write/read (scratch sector 7) ---\n");
    printf_dma_flush();

    /* Guard runs first: if the scratch sector overlaps the running
     * image, every other test in this tier would happily nuke it. */
    RUN_TEST(test_flash_scratch_sector_is_safe);
    RUN_TEST(test_flash_erase_scratch_sector);
    RUN_TEST(test_flash_write_word_readback);
    RUN_TEST(test_flash_write_bytes_readback);
    RUN_TEST(test_flash_sector_info);

    /* ----------------------------------------------------------
     * Tier 7: CRC hardware tests
     *   Pure internal peripheral — no external wiring.
     *   Validates CRC-32 (MPEG-2 polynomial 0x04C11DB7).
     * ---------------------------------------------------------- */
    printf("\n--- Tier 7: CRC hardware ---\n");
    printf_dma_flush();

    RUN_TEST(test_crc_hw_known_vector);
    RUN_TEST(test_crc_hw_reset_restores_init);
    RUN_TEST(test_crc_hw_accumulate_matches_sequential);
    RUN_TEST(test_crc_hw_flash_region);
    RUN_TEST(test_crc_hw_performance);

    /* ----------------------------------------------------------
     * Tier 8: Low-power Stop mode test
     *   Uses EXTI software trigger to wake immediately.
     *   No external wiring required.
     * ---------------------------------------------------------- */
    printf("\n--- Tier 8: Stop mode enter/wake ---\n");
    printf_dma_flush();

    RUN_TEST(test_stop_mode_enter_and_wake);

    /* ----------------------------------------------------------
     * Tier 9: Software BPSK modem (PRBS + AWGN)
     *   Self-contained DSP chain — no external wiring.
     *   Asserts BER tracks theory and cycles/bit under budget.
     * ---------------------------------------------------------- */
    printf("\n--- Tier 9: Software BPSK modem (PRBS+AWGN) ---\n");
    printf_dma_flush();

    RUN_TEST(test_modem_bpsk_ber_awgn);
    printf_dma_flush();

    /* Tier 9b: same chain with RRC pulse shaping + matched filter (#207). */
    printf("\n--- Tier 9b: Software BPSK modem + RRC shaping ---\n");
    printf_dma_flush();

    RUN_TEST(test_modem_bpsk_ber_awgn_shaped);
    printf_dma_flush();

    /* Tier 9c: impaired channel + RX recovery loops (#197, Plan 002 B0.5). */
    printf("\n--- Tier 9c: Software BPSK modem + impairments/recovery ---\n");
    printf_dma_flush();

    RUN_TEST(test_modem_bpsk_recover_sync);

    printf_dma_flush();
    return UNITY_END();
}

#endif /* HIL_TEST_MODE */
