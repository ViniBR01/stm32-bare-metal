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
 */

#ifdef HIL_TEST_MODE

#include "unity.h"
#include "spi_perf.h"
#include "test_output.h"
#include "printf.h"
#include "printf_dma.h"
#include "rcc.h"
#include "timer.h"
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

    printf_dma_flush();
    return UNITY_END();
}

#endif /* HIL_TEST_MODE */
