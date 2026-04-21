/*
 * test_exti.c — Host unit tests for drivers/src/exti_handler.c
 *
 * Uses fake peripheral structs from tests/driver_stubs/ to intercept all
 * register accesses. The EXTI driver runs unmodified on the host — it still
 * writes EXTI->IMR, SYSCFG->EXTICR[], NVIC etc., but those macros now point
 * at global fake structs in SRAM that tests can inspect.
 *
 * setUp() zeroes all fake structs via test_periph_reset() before each test.
 *
 * Bit-flag constants and IRQn values come from the real stm32f411xe.h device
 * header — they are guaranteed to match hardware values.
 *
 * Key IRQ numbers (from stm32f411xe.h):
 *   EXTI0_IRQn    = 6  → NVIC ISER[0] bit 6
 *   EXTI1_IRQn    = 7  → NVIC ISER[0] bit 7
 *   EXTI2_IRQn    = 8  → NVIC ISER[0] bit 8
 *   EXTI3_IRQn    = 9  → NVIC ISER[0] bit 9
 *   EXTI4_IRQn    = 10 → NVIC ISER[0] bit 10
 *   EXTI9_5_IRQn  = 23 → NVIC ISER[0] bit 23
 *   EXTI15_10_IRQn= 40 → NVIC ISER[1] bit 8
 */

#include "unity.h"
#include "stm32f4xx.h"    /* stub: TypeDefs + fake peripheral declarations */
#include "error.h"
#include "exti_handler.h" /* EXTI driver API */
#include "gpio_handler.h" /* For gpio_port_t */
#include "irq_priorities.h"

/* ---- Test lifecycle ----------------------------------------------------- */

void setUp(void)
{
    test_periph_reset();
}

void tearDown(void) {}

/* ======================================================================== */
/* exti_configure_gpio_interrupt — SYSCFG EXTICR port mapping               */
/* ======================================================================== */

void test_configure_porta_pin0_sets_exticr0_to_0(void)
{
    exti_configure_gpio_interrupt(GPIO_PORT_A, 0, EXTI_TRIGGER_RISING,
                                  EXTI_MODE_INTERRUPT);
    /* Port A = 0, line 0: EXTICR[0] bits [3:0] = 0 */
    TEST_ASSERT_EQUAL_HEX32(0U, fake_SYSCFG.EXTICR[0] & 0xFU);
}

void test_configure_portb_pin0_sets_exticr0_to_1(void)
{
    exti_configure_gpio_interrupt(GPIO_PORT_B, 0, EXTI_TRIGGER_RISING,
                                  EXTI_MODE_INTERRUPT);
    /* Port B = 1, line 0: EXTICR[0] bits [3:0] = 1 */
    TEST_ASSERT_EQUAL_HEX32(1U, fake_SYSCFG.EXTICR[0] & 0xFU);
}

void test_configure_portc_pin0_sets_exticr0_to_2(void)
{
    exti_configure_gpio_interrupt(GPIO_PORT_C, 0, EXTI_TRIGGER_RISING,
                                  EXTI_MODE_INTERRUPT);
    /* Port C = 2, line 0: EXTICR[0] bits [3:0] = 2 */
    TEST_ASSERT_EQUAL_HEX32(2U, fake_SYSCFG.EXTICR[0] & 0xFU);
}

void test_configure_portd_pin0_sets_exticr0_to_3(void)
{
    exti_configure_gpio_interrupt(GPIO_PORT_D, 0, EXTI_TRIGGER_RISING,
                                  EXTI_MODE_INTERRUPT);
    /* Port D = 3, line 0: EXTICR[0] bits [3:0] = 3 */
    TEST_ASSERT_EQUAL_HEX32(3U, fake_SYSCFG.EXTICR[0] & 0xFU);
}

void test_configure_porte_pin0_sets_exticr0_to_4(void)
{
    exti_configure_gpio_interrupt(GPIO_PORT_E, 0, EXTI_TRIGGER_RISING,
                                  EXTI_MODE_INTERRUPT);
    /* Port E = 4, line 0: EXTICR[0] bits [3:0] = 4 */
    TEST_ASSERT_EQUAL_HEX32(4U, fake_SYSCFG.EXTICR[0] & 0xFU);
}

void test_configure_porth_pin0_sets_exticr0_to_7(void)
{
    exti_configure_gpio_interrupt(GPIO_PORT_H, 0, EXTI_TRIGGER_RISING,
                                  EXTI_MODE_INTERRUPT);
    /* Port H = 7, line 0: EXTICR[0] bits [3:0] = 7 */
    TEST_ASSERT_EQUAL_HEX32(7U, fake_SYSCFG.EXTICR[0] & 0xFU);
}

void test_configure_pin4_uses_exticr1(void)
{
    exti_configure_gpio_interrupt(GPIO_PORT_B, 4, EXTI_TRIGGER_RISING,
                                  EXTI_MODE_INTERRUPT);
    /* Line 4: reg_index=4/4=1, bit_pos=(4%4)*4=0 → EXTICR[1] bits [3:0] = 1 */
    TEST_ASSERT_EQUAL_HEX32(1U, fake_SYSCFG.EXTICR[1] & 0xFU);
}

void test_configure_pin8_uses_exticr2(void)
{
    exti_configure_gpio_interrupt(GPIO_PORT_C, 8, EXTI_TRIGGER_RISING,
                                  EXTI_MODE_INTERRUPT);
    /* Line 8: reg_index=8/4=2, bit_pos=(8%4)*4=0 → EXTICR[2] bits [3:0] = 2 */
    TEST_ASSERT_EQUAL_HEX32(2U, fake_SYSCFG.EXTICR[2] & 0xFU);
}

void test_configure_pin12_uses_exticr3(void)
{
    exti_configure_gpio_interrupt(GPIO_PORT_A, 12, EXTI_TRIGGER_RISING,
                                  EXTI_MODE_INTERRUPT);
    /* Line 12: reg_index=12/4=3, bit_pos=(12%4)*4=0 → EXTICR[3] bits [3:0] = 0 */
    TEST_ASSERT_EQUAL_HEX32(0U, fake_SYSCFG.EXTICR[3] & 0xFU);
}

void test_configure_pin5_uses_exticr1_bits_7_4(void)
{
    exti_configure_gpio_interrupt(GPIO_PORT_B, 5, EXTI_TRIGGER_RISING,
                                  EXTI_MODE_INTERRUPT);
    /* Line 5: reg_index=5/4=1, bit_pos=(5%4)*4=4 → EXTICR[1] bits [7:4] = 1 */
    TEST_ASSERT_EQUAL_HEX32(1U << 4, fake_SYSCFG.EXTICR[1] & 0xF0U);
}

void test_configure_clears_previous_port_mapping(void)
{
    /* First configure port C on pin 0 */
    exti_configure_gpio_interrupt(GPIO_PORT_C, 0, EXTI_TRIGGER_RISING,
                                  EXTI_MODE_INTERRUPT);
    /* Then reconfigure with port A */
    exti_configure_gpio_interrupt(GPIO_PORT_A, 0, EXTI_TRIGGER_RISING,
                                  EXTI_MODE_INTERRUPT);
    /* Port A = 0: bits should be cleared */
    TEST_ASSERT_EQUAL_HEX32(0U, fake_SYSCFG.EXTICR[0] & 0xFU);
}

/* ======================================================================== */
/* exti_configure_gpio_interrupt — trigger type (RTSR/FTSR)                  */
/* ======================================================================== */

void test_configure_rising_trigger_sets_rtsr(void)
{
    exti_configure_gpio_interrupt(GPIO_PORT_A, 5, EXTI_TRIGGER_RISING,
                                  EXTI_MODE_INTERRUPT);
    TEST_ASSERT_BITS_HIGH(1U << 5, fake_EXTI.RTSR);
    TEST_ASSERT_BITS_LOW(1U << 5, fake_EXTI.FTSR);
}

void test_configure_falling_trigger_sets_ftsr(void)
{
    exti_configure_gpio_interrupt(GPIO_PORT_A, 5, EXTI_TRIGGER_FALLING,
                                  EXTI_MODE_INTERRUPT);
    TEST_ASSERT_BITS_LOW(1U << 5, fake_EXTI.RTSR);
    TEST_ASSERT_BITS_HIGH(1U << 5, fake_EXTI.FTSR);
}

void test_configure_both_triggers_sets_rtsr_and_ftsr(void)
{
    exti_configure_gpio_interrupt(GPIO_PORT_A, 5, EXTI_TRIGGER_BOTH,
                                  EXTI_MODE_INTERRUPT);
    TEST_ASSERT_BITS_HIGH(1U << 5, fake_EXTI.RTSR);
    TEST_ASSERT_BITS_HIGH(1U << 5, fake_EXTI.FTSR);
}

void test_configure_rising_then_falling_clears_rtsr(void)
{
    /* First set rising */
    exti_configure_gpio_interrupt(GPIO_PORT_A, 5, EXTI_TRIGGER_RISING,
                                  EXTI_MODE_INTERRUPT);
    /* Then reconfigure with falling only */
    exti_configure_gpio_interrupt(GPIO_PORT_A, 5, EXTI_TRIGGER_FALLING,
                                  EXTI_MODE_INTERRUPT);
    TEST_ASSERT_BITS_LOW(1U << 5, fake_EXTI.RTSR);
    TEST_ASSERT_BITS_HIGH(1U << 5, fake_EXTI.FTSR);
}

/* ======================================================================== */
/* exti_configure_gpio_interrupt — mode (IMR/EMR)                            */
/* ======================================================================== */

void test_configure_interrupt_mode_sets_imr(void)
{
    exti_configure_gpio_interrupt(GPIO_PORT_A, 5, EXTI_TRIGGER_RISING,
                                  EXTI_MODE_INTERRUPT);
    TEST_ASSERT_BITS_HIGH(1U << 5, fake_EXTI.IMR);
    TEST_ASSERT_BITS_LOW(1U << 5, fake_EXTI.EMR);
}

void test_configure_event_mode_sets_emr(void)
{
    exti_configure_gpio_interrupt(GPIO_PORT_A, 5, EXTI_TRIGGER_RISING,
                                  EXTI_MODE_EVENT);
    TEST_ASSERT_BITS_LOW(1U << 5, fake_EXTI.IMR);
    TEST_ASSERT_BITS_HIGH(1U << 5, fake_EXTI.EMR);
}

void test_configure_both_modes_sets_imr_and_emr(void)
{
    exti_configure_gpio_interrupt(GPIO_PORT_A, 5, EXTI_TRIGGER_RISING,
                                  EXTI_MODE_BOTH);
    TEST_ASSERT_BITS_HIGH(1U << 5, fake_EXTI.IMR);
    TEST_ASSERT_BITS_HIGH(1U << 5, fake_EXTI.EMR);
}

/* ======================================================================== */
/* exti_configure_gpio_interrupt — NVIC enable                               */
/* ======================================================================== */

void test_configure_pin0_enables_nvic_exti0(void)
{
    exti_configure_gpio_interrupt(GPIO_PORT_A, 0, EXTI_TRIGGER_RISING,
                                  EXTI_MODE_INTERRUPT);
    /* EXTI0_IRQn = 6 → ISER[0] bit 6 */
    TEST_ASSERT_BITS_HIGH(1U << 6, fake_NVIC.ISER[0]);
}

void test_configure_pin4_enables_nvic_exti4(void)
{
    exti_configure_gpio_interrupt(GPIO_PORT_A, 4, EXTI_TRIGGER_RISING,
                                  EXTI_MODE_INTERRUPT);
    /* EXTI4_IRQn = 10 → ISER[0] bit 10 */
    TEST_ASSERT_BITS_HIGH(1U << 10, fake_NVIC.ISER[0]);
}

void test_configure_pin7_enables_nvic_exti9_5(void)
{
    exti_configure_gpio_interrupt(GPIO_PORT_A, 7, EXTI_TRIGGER_RISING,
                                  EXTI_MODE_INTERRUPT);
    /* EXTI9_5_IRQn = 23 → ISER[0] bit 23 */
    TEST_ASSERT_BITS_HIGH(1U << 23, fake_NVIC.ISER[0]);
}

void test_configure_pin12_enables_nvic_exti15_10(void)
{
    exti_configure_gpio_interrupt(GPIO_PORT_A, 12, EXTI_TRIGGER_RISING,
                                  EXTI_MODE_INTERRUPT);
    /* EXTI15_10_IRQn = 40 → ISER[1] bit 8 */
    TEST_ASSERT_BITS_HIGH(1U << 8, fake_NVIC.ISER[1]);
}

/* ======================================================================== */
/* exti_configure_gpio_interrupt — configures GPIO pin as input              */
/* ======================================================================== */

void test_configure_sets_gpio_input_mode(void)
{
    exti_configure_gpio_interrupt(GPIO_PORT_A, 5, EXTI_TRIGGER_RISING,
                                  EXTI_MODE_INTERRUPT);
    /* gpio_configure_pin sets MODER bits for pin 5 to 00 (input) */
    TEST_ASSERT_EQUAL_HEX32(0U, fake_GPIOA.MODER & (3U << (5 * 2)));
}

void test_configure_enables_syscfg_clock(void)
{
    exti_configure_gpio_interrupt(GPIO_PORT_A, 5, EXTI_TRIGGER_RISING,
                                  EXTI_MODE_INTERRUPT);
    TEST_ASSERT_BITS_HIGH(RCC_APB2ENR_SYSCFGEN, fake_RCC.APB2ENR);
}

/* ======================================================================== */
/* exti_configure_gpio_interrupt — invalid parameters                        */
/* ======================================================================== */

void test_configure_invalid_pin_returns_error(void)
{
    int ret = exti_configure_gpio_interrupt(GPIO_PORT_A, 16,
                                            EXTI_TRIGGER_RISING,
                                            EXTI_MODE_INTERRUPT);
    TEST_ASSERT_EQUAL(ERR_INVALID_ARG, ret);
}

void test_configure_invalid_trigger_returns_error(void)
{
    int ret = exti_configure_gpio_interrupt(GPIO_PORT_A, 5,
                                            EXTI_TRIGGER_INVALID,
                                            EXTI_MODE_INTERRUPT);
    TEST_ASSERT_EQUAL(ERR_INVALID_ARG, ret);
}

void test_configure_invalid_mode_returns_error(void)
{
    int ret = exti_configure_gpio_interrupt(GPIO_PORT_A, 5,
                                            EXTI_TRIGGER_RISING,
                                            EXTI_MODE_INVALID);
    TEST_ASSERT_EQUAL(ERR_INVALID_ARG, ret);
}

/* ======================================================================== */
/* exti_enable_line / exti_disable_line — NVIC ISER/ICER                     */
/* ======================================================================== */

void test_enable_line0_sets_nvic_iser(void)
{
    int ret = exti_enable_line(0);
    TEST_ASSERT_EQUAL(ERR_OK, ret);
    /* EXTI0_IRQn = 6 → ISER[0] bit 6 */
    TEST_ASSERT_BITS_HIGH(1U << 6, fake_NVIC.ISER[0]);
}

void test_enable_line1_sets_nvic_iser(void)
{
    int ret = exti_enable_line(1);
    TEST_ASSERT_EQUAL(ERR_OK, ret);
    /* EXTI1_IRQn = 7 → ISER[0] bit 7 */
    TEST_ASSERT_BITS_HIGH(1U << 7, fake_NVIC.ISER[0]);
}

void test_enable_line5_sets_nvic_iser_exti9_5(void)
{
    int ret = exti_enable_line(5);
    TEST_ASSERT_EQUAL(ERR_OK, ret);
    /* EXTI9_5_IRQn = 23 → ISER[0] bit 23 */
    TEST_ASSERT_BITS_HIGH(1U << 23, fake_NVIC.ISER[0]);
}

void test_enable_line10_sets_nvic_iser_exti15_10(void)
{
    int ret = exti_enable_line(10);
    TEST_ASSERT_EQUAL(ERR_OK, ret);
    /* EXTI15_10_IRQn = 40 → ISER[1] bit 8 */
    TEST_ASSERT_BITS_HIGH(1U << 8, fake_NVIC.ISER[1]);
}

void test_enable_line15_sets_nvic_iser_exti15_10(void)
{
    int ret = exti_enable_line(15);
    TEST_ASSERT_EQUAL(ERR_OK, ret);
    /* EXTI15_10_IRQn = 40 → ISER[1] bit 8 */
    TEST_ASSERT_BITS_HIGH(1U << 8, fake_NVIC.ISER[1]);
}

void test_enable_invalid_line_returns_error(void)
{
    int ret = exti_enable_line(23);  /* Valid EXTI line but no GPIO IRQ */
    /* Lines 16-22 map to (IRQn_Type)-1, should return ERR_INVALID_ARG */
    TEST_ASSERT_EQUAL(ERR_INVALID_ARG, ret);
}

void test_enable_out_of_range_line_returns_error(void)
{
    int ret = exti_enable_line(25);
    TEST_ASSERT_EQUAL(ERR_INVALID_ARG, ret);
}

void test_disable_line0_sets_nvic_icer(void)
{
    int ret = exti_disable_line(0);
    TEST_ASSERT_EQUAL(ERR_OK, ret);
    /* EXTI0_IRQn = 6 → ICER[0] bit 6 */
    TEST_ASSERT_BITS_HIGH(1U << 6, fake_NVIC.ICER[0]);
}

void test_disable_line4_sets_nvic_icer(void)
{
    int ret = exti_disable_line(4);
    TEST_ASSERT_EQUAL(ERR_OK, ret);
    /* EXTI4_IRQn = 10 → ICER[0] bit 10 */
    TEST_ASSERT_BITS_HIGH(1U << 10, fake_NVIC.ICER[0]);
}

void test_disable_line12_sets_nvic_icer_exti15_10(void)
{
    int ret = exti_disable_line(12);
    TEST_ASSERT_EQUAL(ERR_OK, ret);
    /* EXTI15_10_IRQn = 40 → ICER[1] bit 8 */
    TEST_ASSERT_BITS_HIGH(1U << 8, fake_NVIC.ICER[1]);
}

void test_disable_invalid_line_returns_error(void)
{
    int ret = exti_disable_line(25);
    TEST_ASSERT_EQUAL(ERR_INVALID_ARG, ret);
}

/* ======================================================================== */
/* exti_set_interrupt_mask — EXTI IMR                                        */
/* ======================================================================== */

void test_set_interrupt_mask_enable_sets_imr_bit(void)
{
    int ret = exti_set_interrupt_mask(5, 1);
    TEST_ASSERT_EQUAL(ERR_OK, ret);
    TEST_ASSERT_BITS_HIGH(1U << 5, fake_EXTI.IMR);
}

void test_set_interrupt_mask_disable_clears_imr_bit(void)
{
    fake_EXTI.IMR = (1U << 5);
    int ret = exti_set_interrupt_mask(5, 0);
    TEST_ASSERT_EQUAL(ERR_OK, ret);
    TEST_ASSERT_BITS_LOW(1U << 5, fake_EXTI.IMR);
}

void test_set_interrupt_mask_does_not_affect_other_lines(void)
{
    fake_EXTI.IMR = (1U << 3);
    exti_set_interrupt_mask(5, 1);
    TEST_ASSERT_BITS_HIGH(1U << 3, fake_EXTI.IMR);
    TEST_ASSERT_BITS_HIGH(1U << 5, fake_EXTI.IMR);
}

void test_set_interrupt_mask_invalid_line_returns_error(void)
{
    int ret = exti_set_interrupt_mask(25, 1);
    TEST_ASSERT_EQUAL(ERR_INVALID_ARG, ret);
}

/* ======================================================================== */
/* exti_set_event_mask — EXTI EMR                                            */
/* ======================================================================== */

void test_set_event_mask_enable_sets_emr_bit(void)
{
    int ret = exti_set_event_mask(5, 1);
    TEST_ASSERT_EQUAL(ERR_OK, ret);
    TEST_ASSERT_BITS_HIGH(1U << 5, fake_EXTI.EMR);
}

void test_set_event_mask_disable_clears_emr_bit(void)
{
    fake_EXTI.EMR = (1U << 5);
    int ret = exti_set_event_mask(5, 0);
    TEST_ASSERT_EQUAL(ERR_OK, ret);
    TEST_ASSERT_BITS_LOW(1U << 5, fake_EXTI.EMR);
}

void test_set_event_mask_does_not_affect_other_lines(void)
{
    fake_EXTI.EMR = (1U << 7);
    exti_set_event_mask(5, 1);
    TEST_ASSERT_BITS_HIGH(1U << 7, fake_EXTI.EMR);
    TEST_ASSERT_BITS_HIGH(1U << 5, fake_EXTI.EMR);
}

void test_set_event_mask_invalid_line_returns_error(void)
{
    int ret = exti_set_event_mask(25, 1);
    TEST_ASSERT_EQUAL(ERR_INVALID_ARG, ret);
}

/* ======================================================================== */
/* exti_is_pending — EXTI PR                                                 */
/* ======================================================================== */

void test_is_pending_returns_1_when_pr_bit_set(void)
{
    fake_EXTI.PR = (1U << 5);
    int ret = exti_is_pending(5);
    TEST_ASSERT_EQUAL(1, ret);
}

void test_is_pending_returns_0_when_pr_bit_clear(void)
{
    fake_EXTI.PR = 0U;
    int ret = exti_is_pending(5);
    TEST_ASSERT_EQUAL(0, ret);
}

void test_is_pending_only_checks_requested_line(void)
{
    fake_EXTI.PR = (1U << 3);  /* line 3 pending, line 5 not */
    TEST_ASSERT_EQUAL(0, exti_is_pending(5));
    TEST_ASSERT_EQUAL(1, exti_is_pending(3));
}

void test_is_pending_invalid_line_returns_error(void)
{
    int ret = exti_is_pending(25);
    TEST_ASSERT_EQUAL(ERR_INVALID_ARG, ret);
}

/* ======================================================================== */
/* exti_clear_pending — EXTI PR write-1-to-clear                             */
/* ======================================================================== */

void test_clear_pending_writes_1_to_pr_bit(void)
{
    int ret = exti_clear_pending(5);
    TEST_ASSERT_EQUAL(ERR_OK, ret);
    /* PR is write-1-to-clear on hardware; in the fake struct, the driver
     * writes 1 to the bit. We verify the write happened. */
    TEST_ASSERT_BITS_HIGH(1U << 5, fake_EXTI.PR);
}

void test_clear_pending_invalid_line_returns_error(void)
{
    int ret = exti_clear_pending(25);
    TEST_ASSERT_EQUAL(ERR_INVALID_ARG, ret);
}

/* ======================================================================== */
/* exti_software_trigger — EXTI SWIER                                        */
/* ======================================================================== */

void test_software_trigger_sets_swier_bit(void)
{
    int ret = exti_software_trigger(5);
    TEST_ASSERT_EQUAL(ERR_OK, ret);
    TEST_ASSERT_BITS_HIGH(1U << 5, fake_EXTI.SWIER);
}

void test_software_trigger_line0_sets_swier_bit0(void)
{
    exti_software_trigger(0);
    TEST_ASSERT_BITS_HIGH(1U << 0, fake_EXTI.SWIER);
}

void test_software_trigger_does_not_affect_other_lines(void)
{
    exti_software_trigger(5);
    TEST_ASSERT_BITS_LOW(1U << 3, fake_EXTI.SWIER);
    TEST_ASSERT_BITS_HIGH(1U << 5, fake_EXTI.SWIER);
}

void test_software_trigger_invalid_line_returns_error(void)
{
    int ret = exti_software_trigger(25);
    TEST_ASSERT_EQUAL(ERR_INVALID_ARG, ret);
}

/* ======================================================================== */
/* exti_configure_gpio_interrupt — NVIC priority                             */
/* ======================================================================== */

void test_configure_pin0_nvic_priority_is_exti(void)
{
    exti_configure_gpio_interrupt(GPIO_PORT_A, 0, EXTI_TRIGGER_RISING,
                                  EXTI_MODE_INTERRUPT);
    /* EXTI0_IRQn = 6 */
    TEST_ASSERT_EQUAL(IRQ_PRIO_EXTI << (8U - __NVIC_PRIO_BITS),
                      fake_NVIC.IP[(uint32_t)EXTI0_IRQn]);
}

void test_configure_pin7_nvic_priority_is_exti(void)
{
    exti_configure_gpio_interrupt(GPIO_PORT_A, 7, EXTI_TRIGGER_RISING,
                                  EXTI_MODE_INTERRUPT);
    /* EXTI9_5_IRQn = 23 */
    TEST_ASSERT_EQUAL(IRQ_PRIO_EXTI << (8U - __NVIC_PRIO_BITS),
                      fake_NVIC.IP[(uint32_t)EXTI9_5_IRQn]);
}

void test_configure_pin10_nvic_priority_is_exti(void)
{
    exti_configure_gpio_interrupt(GPIO_PORT_A, 10, EXTI_TRIGGER_RISING,
                                  EXTI_MODE_INTERRUPT);
    /* EXTI15_10_IRQn = 40 */
    TEST_ASSERT_EQUAL(IRQ_PRIO_EXTI << (8U - __NVIC_PRIO_BITS),
                      fake_NVIC.IP[(uint32_t)EXTI15_10_IRQn]);
}

/* ======================================================================== */
/* main                                                                      */
/* ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* SYSCFG EXTICR port mapping */
    RUN_TEST(test_configure_porta_pin0_sets_exticr0_to_0);
    RUN_TEST(test_configure_portb_pin0_sets_exticr0_to_1);
    RUN_TEST(test_configure_portc_pin0_sets_exticr0_to_2);
    RUN_TEST(test_configure_portd_pin0_sets_exticr0_to_3);
    RUN_TEST(test_configure_porte_pin0_sets_exticr0_to_4);
    RUN_TEST(test_configure_porth_pin0_sets_exticr0_to_7);
    RUN_TEST(test_configure_pin4_uses_exticr1);
    RUN_TEST(test_configure_pin8_uses_exticr2);
    RUN_TEST(test_configure_pin12_uses_exticr3);
    RUN_TEST(test_configure_pin5_uses_exticr1_bits_7_4);
    RUN_TEST(test_configure_clears_previous_port_mapping);

    /* Trigger type (RTSR/FTSR) */
    RUN_TEST(test_configure_rising_trigger_sets_rtsr);
    RUN_TEST(test_configure_falling_trigger_sets_ftsr);
    RUN_TEST(test_configure_both_triggers_sets_rtsr_and_ftsr);
    RUN_TEST(test_configure_rising_then_falling_clears_rtsr);

    /* Mode (IMR/EMR) */
    RUN_TEST(test_configure_interrupt_mode_sets_imr);
    RUN_TEST(test_configure_event_mode_sets_emr);
    RUN_TEST(test_configure_both_modes_sets_imr_and_emr);

    /* NVIC enable via configure */
    RUN_TEST(test_configure_pin0_enables_nvic_exti0);
    RUN_TEST(test_configure_pin4_enables_nvic_exti4);
    RUN_TEST(test_configure_pin7_enables_nvic_exti9_5);
    RUN_TEST(test_configure_pin12_enables_nvic_exti15_10);

    /* GPIO input + SYSCFG clock via configure */
    RUN_TEST(test_configure_sets_gpio_input_mode);
    RUN_TEST(test_configure_enables_syscfg_clock);

    /* Invalid parameter handling for configure */
    RUN_TEST(test_configure_invalid_pin_returns_error);
    RUN_TEST(test_configure_invalid_trigger_returns_error);
    RUN_TEST(test_configure_invalid_mode_returns_error);

    /* exti_enable_line */
    RUN_TEST(test_enable_line0_sets_nvic_iser);
    RUN_TEST(test_enable_line1_sets_nvic_iser);
    RUN_TEST(test_enable_line5_sets_nvic_iser_exti9_5);
    RUN_TEST(test_enable_line10_sets_nvic_iser_exti15_10);
    RUN_TEST(test_enable_line15_sets_nvic_iser_exti15_10);
    RUN_TEST(test_enable_invalid_line_returns_error);
    RUN_TEST(test_enable_out_of_range_line_returns_error);

    /* exti_disable_line */
    RUN_TEST(test_disable_line0_sets_nvic_icer);
    RUN_TEST(test_disable_line4_sets_nvic_icer);
    RUN_TEST(test_disable_line12_sets_nvic_icer_exti15_10);
    RUN_TEST(test_disable_invalid_line_returns_error);

    /* exti_set_interrupt_mask */
    RUN_TEST(test_set_interrupt_mask_enable_sets_imr_bit);
    RUN_TEST(test_set_interrupt_mask_disable_clears_imr_bit);
    RUN_TEST(test_set_interrupt_mask_does_not_affect_other_lines);
    RUN_TEST(test_set_interrupt_mask_invalid_line_returns_error);

    /* exti_set_event_mask */
    RUN_TEST(test_set_event_mask_enable_sets_emr_bit);
    RUN_TEST(test_set_event_mask_disable_clears_emr_bit);
    RUN_TEST(test_set_event_mask_does_not_affect_other_lines);
    RUN_TEST(test_set_event_mask_invalid_line_returns_error);

    /* exti_is_pending */
    RUN_TEST(test_is_pending_returns_1_when_pr_bit_set);
    RUN_TEST(test_is_pending_returns_0_when_pr_bit_clear);
    RUN_TEST(test_is_pending_only_checks_requested_line);
    RUN_TEST(test_is_pending_invalid_line_returns_error);

    /* exti_clear_pending */
    RUN_TEST(test_clear_pending_writes_1_to_pr_bit);
    RUN_TEST(test_clear_pending_invalid_line_returns_error);

    /* exti_software_trigger */
    RUN_TEST(test_software_trigger_sets_swier_bit);
    RUN_TEST(test_software_trigger_line0_sets_swier_bit0);
    RUN_TEST(test_software_trigger_does_not_affect_other_lines);
    RUN_TEST(test_software_trigger_invalid_line_returns_error);

    /* NVIC priority */
    RUN_TEST(test_configure_pin0_nvic_priority_is_exti);
    RUN_TEST(test_configure_pin7_nvic_priority_is_exti);
    RUN_TEST(test_configure_pin10_nvic_priority_is_exti);

    return UNITY_END();
}
