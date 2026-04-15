/*
 * test_uart.c — Host unit tests for drivers/src/uart.c
 *
 * Two tiers of tests:
 *
 * Tier 2 — Pure function tests (uart_calc.h)
 *   uart_compute_baud_divisor, uart_circ_bytes_available
 *   No register access; test the mathematical logic directly.
 *
 * Tier 1 — Register configuration tests
 *   uart_init, uart_write, uart_read, uart_write_dma,
 *   uart_start_rx_dma, uart_stop_rx_dma, error flag handling,
 *   and RX callback dispatch (via direct USART2_IRQHandler() call).
 *
 *   Note: uart_read() and uart_write() poll USART2->SR flags.
 *   Tests that exercise these functions must pre-seed the SR register
 *   (SR_TXE / SR_RXNE) so the polling loops exit immediately.
 *
 *   Note: uart_init() calls dma_stream_init() which tracks allocation state
 *   in a static table inside dma.c. After the first uart_init(), the TX
 *   stream (DMA1_Stream6) stays allocated across test cases even after
 *   test_periph_reset(). DMA register checks that depend solely on
 *   dma_stream_init() (PAR, CR base) are therefore only reliable on the
 *   first call and are limited to the dedicated init tests below.
 *
 * setUp() zeros all fakes and seeds a 16 MHz clock via rcc_init() so that
 * rcc_get_apb1_clk() returns a known value for BRR tests.
 */

#include "unity.h"
#include "stm32f4xx.h"  /* stub: TypeDefs + fake peripheral declarations */
#include "rcc.h"
#include "uart.h"
#include "uart_calc.h"

/* Timer clock seeded in setUp() — 16 MHz (HSI direct, no PLL) */
#define TEST_APB1_CLK_HZ  16000000U

/* Mirror bit definitions from uart.c (not in the public header) */
#define SR_TXE     (1U << 7)
#define SR_RXNE    (1U << 5)
#define SR_IDLE    (1U << 4)
#define SR_ORE     (1U << 3)
#define SR_FE      (1U << 1)
#define SR_NF      (1U << 2)
#define CR1_TE     (1U << 3)
#define CR1_RE     (1U << 2)
#define CR1_UE     (1U << 13)
#define CR1_RXNEIE (1U << 5)
#define CR1_IDLEIE (1U << 4)
#define CR3_DMAT   (1U << 7)
#define CR3_DMAR   (1U << 6)
#define CR3_EIE    (1U << 0)

/* Forward-declare ISRs so we can call them directly to simulate interrupts */
extern void USART2_IRQHandler(void);
extern void DMA1_Stream6_IRQHandler(void);

/*
 * uart_write_dma() sets an internal static tx_busy flag that is not cleared
 * by test_periph_reset().  Simulate a DMA1 Stream6 transfer-complete interrupt
 * to invoke uart_tx_dma_tc_callback() and reset tx_busy to 0.
 *
 * Stream6 uses DMA1->HISR (streams 4–7), flag group FLAG_BASE_2 (bit offset 16):
 *   TCIF = bit 21
 */
static void reset_tx_busy(void)
{
    fake_DMA1.HISR |= (1U << 21);
    DMA1_Stream6_IRQHandler();
}

/* ---- Test lifecycle ------------------------------------------------------- */

static char  rx_cb_char;
static int   rx_cb_count;

static void test_rx_cb(char ch) { rx_cb_char = ch; rx_cb_count++; }

void setUp(void)
{
    test_periph_reset();
    rx_cb_char  = 0;
    rx_cb_count = 0;
    /* Seed clock cache so rcc_get_apb1_clk() returns TEST_APB1_CLK_HZ */
    rcc_init(RCC_CLK_SRC_HSI, TEST_APB1_CLK_HZ);
}

void tearDown(void) {}

/* ======================================================================== */
/* uart_compute_baud_divisor                                                  */
/* ======================================================================== */

void test_baud_divisor_50mhz_115200_is_434(void)
{
    /* (50 000 000 + 57 600) / 115 200 = 434 */
    TEST_ASSERT_EQUAL(434U, uart_compute_baud_divisor(50000000U, 115200U));
}

void test_baud_divisor_16mhz_115200_is_139(void)
{
    /* (16 000 000 + 57 600) / 115 200 = 139 */
    TEST_ASSERT_EQUAL(139U, uart_compute_baud_divisor(16000000U, 115200U));
}

void test_baud_divisor_50mhz_9600_is_5208(void)
{
    /* (50 000 000 + 4 800) / 9 600 = 5208 */
    TEST_ASSERT_EQUAL(5208U, uart_compute_baud_divisor(50000000U, 9600U));
}

void test_baud_divisor_100mhz_115200_is_868(void)
{
    /* (100 000 000 + 57 600) / 115 200 = 868 */
    TEST_ASSERT_EQUAL(868U, uart_compute_baud_divisor(100000000U, 115200U));
}

void test_baud_divisor_rounds_up_at_half(void)
{
    /* 172 800 = 1.5 × 115 200; with +half rounding: (172800 + 57600) / 115200 = 2 */
    TEST_ASSERT_EQUAL(2U, uart_compute_baud_divisor(172800U, 115200U));
}

void test_baud_divisor_rounds_down_just_below_half(void)
{
    /* 172 799 < 1.5 × 115 200; (172799 + 57600) / 115200 = 1 */
    TEST_ASSERT_EQUAL(1U, uart_compute_baud_divisor(172799U, 115200U));
}

/* ======================================================================== */
/* uart_circ_bytes_available                                                  */
/* ======================================================================== */

void test_circ_no_wrap_from_start(void)
{
    /* ndtr=90 → head=10; last_ndtr=100 → tail=0; bytes = 10 */
    TEST_ASSERT_EQUAL(10U, uart_circ_bytes_available(90U, 100U, 100U));
}

void test_circ_no_wrap_mid_buffer(void)
{
    /* ndtr=50 → head=50; last_ndtr=80 → tail=20; bytes = 30 */
    TEST_ASSERT_EQUAL(30U, uart_circ_bytes_available(50U, 80U, 100U));
}

void test_circ_no_wrap_full_buffer(void)
{
    /* ndtr=0 → head=100; last_ndtr=100 → tail=0; bytes = 100 */
    TEST_ASSERT_EQUAL(100U, uart_circ_bytes_available(0U, 100U, 100U));
}

void test_circ_no_change_returns_zero(void)
{
    /* head == tail: 0 bytes */
    TEST_ASSERT_EQUAL(0U, uart_circ_bytes_available(80U, 80U, 100U));
}

void test_circ_wrap_around(void)
{
    /* ndtr=90 → head=10; last_ndtr=10 → tail=90; wrap: (100-90)+10 = 20 */
    TEST_ASSERT_EQUAL(20U, uart_circ_bytes_available(90U, 10U, 100U));
}

void test_circ_wrap_tight(void)
{
    /* ndtr=95 → head=5; last_ndtr=5 → tail=95; wrap: (100-95)+5 = 10 */
    TEST_ASSERT_EQUAL(10U, uart_circ_bytes_available(95U, 5U, 100U));
}

/* ======================================================================== */
/* uart_init — clock enables                                                  */
/* ======================================================================== */

void test_uart_init_enables_gpioa_clock(void)
{
    uart_init();
    TEST_ASSERT_BITS_HIGH(RCC_AHB1ENR_GPIOAEN, fake_RCC.AHB1ENR);
}

void test_uart_init_enables_usart2_apb1_clock(void)
{
    uart_init();
    TEST_ASSERT_BITS_HIGH(1U << 17, fake_RCC.APB1ENR);
}

/* ======================================================================== */
/* uart_init — GPIO alternate function configuration                          */
/* ======================================================================== */

void test_uart_init_pa2_mode_is_af(void)
{
    /* PA2 MODER bits [5:4] must be 0b10 (AF) */
    uart_init();
    TEST_ASSERT_EQUAL_HEX32(0x20U, fake_GPIOA.MODER & 0x30U);
}

void test_uart_init_pa3_mode_is_af(void)
{
    /* PA3 MODER bits [7:6] must be 0b10 (AF) */
    uart_init();
    TEST_ASSERT_EQUAL_HEX32(0x80U, fake_GPIOA.MODER & 0xC0U);
}

void test_uart_init_pa2_af7(void)
{
    /* PA2 AFR[0] bits [11:8] must be 7 */
    uart_init();
    TEST_ASSERT_EQUAL_HEX32(0x700U, fake_GPIOA.AFR[0] & 0xF00U);
}

void test_uart_init_pa3_af7(void)
{
    /* PA3 AFR[0] bits [15:12] must be 7 */
    uart_init();
    TEST_ASSERT_EQUAL_HEX32(0x7000U, fake_GPIOA.AFR[0] & 0xF000U);
}

/* ======================================================================== */
/* uart_init — USART2 register configuration                                  */
/* ======================================================================== */

void test_uart_init_brr_matches_baud_divisor(void)
{
    /* APB1 = 16 MHz seeded in setUp; BRR = uart_compute_baud_divisor(16M,115200) */
    uart_init();
    TEST_ASSERT_EQUAL(uart_compute_baud_divisor(TEST_APB1_CLK_HZ, 115200U),
                      fake_USART2.BRR);
}

void test_uart_init_cr1_te_re_ue_set(void)
{
    uart_init();
    TEST_ASSERT_BITS_HIGH(CR1_TE | CR1_RE | CR1_UE, fake_USART2.CR1);
}

void test_uart_init_cr1_rxneie_and_idleie_set(void)
{
    uart_init();
    TEST_ASSERT_BITS_HIGH(CR1_RXNEIE | CR1_IDLEIE, fake_USART2.CR1);
}

void test_uart_init_cr3_dmat_and_eie_set(void)
{
    uart_init();
    TEST_ASSERT_BITS_HIGH(CR3_DMAT | CR3_EIE, fake_USART2.CR3);
}

/* ======================================================================== */
/* uart_init — NVIC                                                           */
/* ======================================================================== */

void test_uart_init_nvic_usart2_irq_enabled(void)
{
    uart_init();
    uint32_t irqn = (uint32_t)USART2_IRQn;
    TEST_ASSERT_BITS_HIGH(1U << (irqn & 0x1FU), fake_NVIC.ISER[irqn >> 5U]);
}

void test_uart_init_nvic_usart2_priority_is_2(void)
{
    uart_init();
    /* NVIC_SetPriority stores (priority << (8 - __NVIC_PRIO_BITS)) = 2 << 4 = 32 */
    TEST_ASSERT_EQUAL(2U << (8U - 4U), fake_NVIC.IP[(uint32_t)USART2_IRQn]);
}

/* ======================================================================== */
/* uart_write                                                                 */
/* ======================================================================== */

void test_uart_write_puts_char_in_dr(void)
{
    /* Pre-seed TXE so the polling loop exits immediately */
    fake_USART2.SR = SR_TXE;
    uart_write('A');
    TEST_ASSERT_EQUAL('A', (char)(fake_USART2.DR & 0xFFU));
}

void test_uart_write_newline_final_dr_is_lf(void)
{
    /* uart_write('\n') sends '\r' then '\n'; last write to DR is '\n' */
    fake_USART2.SR = SR_TXE;
    uart_write('\n');
    TEST_ASSERT_EQUAL('\n', (char)(fake_USART2.DR & 0xFFU));
}

/* ======================================================================== */
/* uart_read                                                                  */
/* ======================================================================== */

void test_uart_read_returns_dr_value(void)
{
    /* Pre-seed RXNE and DR so the polling loop exits immediately */
    fake_USART2.SR = SR_RXNE;
    fake_USART2.DR = 'Z';
    TEST_ASSERT_EQUAL('Z', uart_read());
}

void test_uart_read_masks_to_8_bits(void)
{
    fake_USART2.SR = SR_RXNE;
    fake_USART2.DR = 0x1FFU;  /* 9-bit value */
    TEST_ASSERT_EQUAL((char)0xFF, uart_read());
}

/* ======================================================================== */
/* uart_write_dma                                                             */
/* ======================================================================== */

void test_uart_write_dma_sets_tx_busy(void)
{
    uart_init();
    TEST_ASSERT_EQUAL(0, uart_is_tx_busy());
    static const char buf[] = "hello";
    uart_write_dma(buf, 5U);
    TEST_ASSERT_EQUAL(1, uart_is_tx_busy());
}

void test_uart_write_dma_zero_length_does_not_set_busy(void)
{
    uart_init();
    reset_tx_busy();  /* clear tx_busy that may linger from a previous test */
    static const char buf[] = "hi";
    uart_write_dma(buf, 0U);
    TEST_ASSERT_EQUAL(0, uart_is_tx_busy());
}

void test_uart_write_dma_sets_ndtr_and_mem_addr(void)
{
    uart_init();
    reset_tx_busy();
    static const char buf[16];
    uart_write_dma(buf, 16U);
    TEST_ASSERT_EQUAL(16U, fake_DMA1_S6.NDTR);
    TEST_ASSERT_EQUAL_HEX32((uint32_t)buf, fake_DMA1_S6.M0AR);
}

void test_uart_write_dma_busy_skips_second_transfer(void)
{
    uart_init();
    reset_tx_busy();
    static const char buf1[4], buf2[4];
    uart_write_dma(buf1, 4U);   /* sets busy, M0AR → buf1 */
    uart_write_dma(buf2, 4U);   /* ignored: still busy */
    /* M0AR must still point at buf1 */
    TEST_ASSERT_EQUAL_HEX32((uint32_t)buf1, fake_DMA1_S6.M0AR);
}

/* ======================================================================== */
/* uart_start_rx_dma / uart_stop_rx_dma                                       */
/* ======================================================================== */

void test_uart_start_rx_dma_enables_dmar_in_cr3(void)
{
    uart_init();
    static uint8_t buf[32];
    uart_start_rx_dma(buf, sizeof(buf));
    TEST_ASSERT_BITS_HIGH(CR3_DMAR, fake_USART2.CR3);
}

void test_uart_start_rx_dma_disables_rxneie(void)
{
    uart_init();
    static uint8_t buf[32];
    uart_start_rx_dma(buf, sizeof(buf));
    TEST_ASSERT_BITS_LOW(CR1_RXNEIE, fake_USART2.CR1);
}

void test_uart_start_rx_dma_sets_ndtr(void)
{
    uart_init();
    static uint8_t buf[64];
    uart_start_rx_dma(buf, 64U);
    TEST_ASSERT_EQUAL(64U, fake_DMA1_S5.NDTR);
}

void test_uart_start_rx_dma_sets_mem_addr(void)
{
    uart_init();
    static uint8_t buf[32];
    uart_start_rx_dma(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_HEX32((uint32_t)buf, fake_DMA1_S5.M0AR);
}

void test_uart_stop_rx_dma_clears_dmar(void)
{
    uart_init();
    static uint8_t buf[32];
    uart_start_rx_dma(buf, sizeof(buf));
    uart_stop_rx_dma();
    TEST_ASSERT_BITS_LOW(CR3_DMAR, fake_USART2.CR3);
}

void test_uart_stop_rx_dma_reenables_rxneie(void)
{
    uart_init();
    static uint8_t buf[32];
    uart_start_rx_dma(buf, sizeof(buf));
    uart_stop_rx_dma();
    TEST_ASSERT_BITS_HIGH(CR1_RXNEIE, fake_USART2.CR1);
}

/* ======================================================================== */
/* Error flags                                                                */
/* ======================================================================== */

void test_overrun_error_set_on_ore_interrupt(void)
{
    fake_USART2.SR = SR_ORE;
    USART2_IRQHandler();
    TEST_ASSERT_EQUAL(1, uart_get_errors().overrun_error);
}

void test_framing_error_set_on_fe_interrupt(void)
{
    fake_USART2.SR = SR_FE;
    USART2_IRQHandler();
    TEST_ASSERT_EQUAL(1, uart_get_errors().framing_error);
}

void test_noise_error_set_on_nf_interrupt(void)
{
    fake_USART2.SR = SR_NF;
    USART2_IRQHandler();
    TEST_ASSERT_EQUAL(1, uart_get_errors().noise_error);
}

void test_uart_clear_errors_resets_all_flags(void)
{
    fake_USART2.SR = SR_ORE | SR_FE | SR_NF;
    USART2_IRQHandler();
    uart_clear_errors();
    uart_error_flags_t err = uart_get_errors();
    TEST_ASSERT_EQUAL(0, err.overrun_error);
    TEST_ASSERT_EQUAL(0, err.framing_error);
    TEST_ASSERT_EQUAL(0, err.noise_error);
}

/* ======================================================================== */
/* RX callback dispatch (simulated via direct IRQ handler call)              */
/* ======================================================================== */

void test_rx_callback_called_on_rxne(void)
{
    uart_register_rx_callback(test_rx_cb);
    fake_USART2.SR = SR_RXNE;
    fake_USART2.DR = 'X';
    USART2_IRQHandler();
    TEST_ASSERT_EQUAL(1, rx_cb_count);
    TEST_ASSERT_EQUAL('X', rx_cb_char);
}

void test_rx_callback_not_called_when_dma_rx_active(void)
{
    uart_init();
    uart_register_rx_callback(test_rx_cb);
    static uint8_t buf[16];
    uart_start_rx_dma(buf, sizeof(buf));

    /* RXNE fires but DMA RX is active — callback must NOT be invoked */
    fake_USART2.SR = SR_RXNE;
    fake_USART2.DR = 'Q';
    USART2_IRQHandler();
    TEST_ASSERT_EQUAL(0, rx_cb_count);

    uart_stop_rx_dma();
}

void test_rx_callback_receives_correct_character(void)
{
    uart_register_rx_callback(test_rx_cb);
    fake_USART2.SR = SR_RXNE;
    fake_USART2.DR = 0x42U; /* 'B' */
    USART2_IRQHandler();
    TEST_ASSERT_EQUAL(0x42, (uint8_t)rx_cb_char);
}

void test_null_rx_callback_does_not_crash(void)
{
    uart_register_rx_callback(NULL);
    fake_USART2.SR = SR_RXNE;
    fake_USART2.DR = 'A';
    USART2_IRQHandler();  /* Must not crash or assert */
    TEST_PASS();
}

/* ======================================================================== */
/* main                                                                       */
/* ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* uart_compute_baud_divisor */
    RUN_TEST(test_baud_divisor_50mhz_115200_is_434);
    RUN_TEST(test_baud_divisor_16mhz_115200_is_139);
    RUN_TEST(test_baud_divisor_50mhz_9600_is_5208);
    RUN_TEST(test_baud_divisor_100mhz_115200_is_868);
    RUN_TEST(test_baud_divisor_rounds_up_at_half);
    RUN_TEST(test_baud_divisor_rounds_down_just_below_half);

    /* uart_circ_bytes_available */
    RUN_TEST(test_circ_no_wrap_from_start);
    RUN_TEST(test_circ_no_wrap_mid_buffer);
    RUN_TEST(test_circ_no_wrap_full_buffer);
    RUN_TEST(test_circ_no_change_returns_zero);
    RUN_TEST(test_circ_wrap_around);
    RUN_TEST(test_circ_wrap_tight);

    /* uart_init — clock enables */
    RUN_TEST(test_uart_init_enables_gpioa_clock);
    RUN_TEST(test_uart_init_enables_usart2_apb1_clock);

    /* uart_init — GPIO AF configuration */
    RUN_TEST(test_uart_init_pa2_mode_is_af);
    RUN_TEST(test_uart_init_pa3_mode_is_af);
    RUN_TEST(test_uart_init_pa2_af7);
    RUN_TEST(test_uart_init_pa3_af7);

    /* uart_init — USART2 registers */
    RUN_TEST(test_uart_init_brr_matches_baud_divisor);
    RUN_TEST(test_uart_init_cr1_te_re_ue_set);
    RUN_TEST(test_uart_init_cr1_rxneie_and_idleie_set);
    RUN_TEST(test_uart_init_cr3_dmat_and_eie_set);

    /* uart_init — NVIC */
    RUN_TEST(test_uart_init_nvic_usart2_irq_enabled);
    RUN_TEST(test_uart_init_nvic_usart2_priority_is_2);

    /* uart_write */
    RUN_TEST(test_uart_write_puts_char_in_dr);
    RUN_TEST(test_uart_write_newline_final_dr_is_lf);

    /* uart_read */
    RUN_TEST(test_uart_read_returns_dr_value);
    RUN_TEST(test_uart_read_masks_to_8_bits);

    /* uart_write_dma */
    RUN_TEST(test_uart_write_dma_sets_tx_busy);
    RUN_TEST(test_uart_write_dma_zero_length_does_not_set_busy);
    RUN_TEST(test_uart_write_dma_sets_ndtr_and_mem_addr);
    RUN_TEST(test_uart_write_dma_busy_skips_second_transfer);

    /* uart_start_rx_dma / uart_stop_rx_dma */
    RUN_TEST(test_uart_start_rx_dma_enables_dmar_in_cr3);
    RUN_TEST(test_uart_start_rx_dma_disables_rxneie);
    RUN_TEST(test_uart_start_rx_dma_sets_ndtr);
    RUN_TEST(test_uart_start_rx_dma_sets_mem_addr);
    RUN_TEST(test_uart_stop_rx_dma_clears_dmar);
    RUN_TEST(test_uart_stop_rx_dma_reenables_rxneie);

    /* Error flags */
    RUN_TEST(test_overrun_error_set_on_ore_interrupt);
    RUN_TEST(test_framing_error_set_on_fe_interrupt);
    RUN_TEST(test_noise_error_set_on_nf_interrupt);
    RUN_TEST(test_uart_clear_errors_resets_all_flags);

    /* RX callback dispatch */
    RUN_TEST(test_rx_callback_called_on_rxne);
    RUN_TEST(test_rx_callback_not_called_when_dma_rx_active);
    RUN_TEST(test_rx_callback_receives_correct_character);
    RUN_TEST(test_null_rx_callback_does_not_crash);

    return UNITY_END();
}
