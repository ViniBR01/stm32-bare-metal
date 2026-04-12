/*
 * test_gpio.c — Host unit tests for drivers/src/gpio_handler.c
 *
 * Uses fake peripheral structs from tests/driver_stubs/ to intercept all
 * register accesses. The GPIO driver runs unmodified on the host — it still
 * writes GPIOA->MODER, RCC->AHB1ENR etc., but those macros now point at
 * global fake structs in SRAM that tests can inspect.
 *
 * setUp() zeroes all fake structs via test_periph_reset() before each test.
 *
 * Bit-flag constants (RCC_AHB1ENR_GPIOAEN, etc.) come from the real
 * stm32f411xe.h device header — they are guaranteed to match hardware values.
 */

#include "unity.h"
#include "stm32f4xx.h"    /* stub: TypeDefs + fake peripheral declarations */
#include "gpio_handler.h" /* GPIO driver API */

/* ---- Test lifecycle ----------------------------------------------------- */

void setUp(void)
{
    test_periph_reset();
}

void tearDown(void) {}

/* ======================================================================== */
/* gpio_clock_enable / gpio_clock_disable                                    */
/* ======================================================================== */

void test_clock_enable_port_a_sets_gpioaen(void)
{
    gpio_clock_enable(GPIO_PORT_A);
    TEST_ASSERT_BITS_HIGH(RCC_AHB1ENR_GPIOAEN, fake_RCC.AHB1ENR);
}

void test_clock_enable_port_b_sets_gpioben(void)
{
    gpio_clock_enable(GPIO_PORT_B);
    TEST_ASSERT_BITS_HIGH(RCC_AHB1ENR_GPIOBEN, fake_RCC.AHB1ENR);
}

void test_clock_enable_port_c_sets_gpiocen(void)
{
    gpio_clock_enable(GPIO_PORT_C);
    TEST_ASSERT_BITS_HIGH(RCC_AHB1ENR_GPIOCEN, fake_RCC.AHB1ENR);
}

void test_clock_enable_does_not_clear_other_bits(void)
{
    /* Pre-set some other bits */
    fake_RCC.AHB1ENR = RCC_AHB1ENR_GPIOBEN;
    gpio_clock_enable(GPIO_PORT_A);
    /* Both A and B should be set */
    TEST_ASSERT_BITS_HIGH(RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN,
                          fake_RCC.AHB1ENR);
}

void test_clock_disable_port_a_clears_gpioaen(void)
{
    fake_RCC.AHB1ENR = RCC_AHB1ENR_GPIOAEN;
    gpio_clock_disable(GPIO_PORT_A);
    TEST_ASSERT_BITS_LOW(RCC_AHB1ENR_GPIOAEN, fake_RCC.AHB1ENR);
}

void test_clock_disable_does_not_clear_other_bits(void)
{
    fake_RCC.AHB1ENR = RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN;
    gpio_clock_disable(GPIO_PORT_A);
    TEST_ASSERT_BITS_HIGH(RCC_AHB1ENR_GPIOBEN, fake_RCC.AHB1ENR);
    TEST_ASSERT_BITS_LOW(RCC_AHB1ENR_GPIOAEN,  fake_RCC.AHB1ENR);
}

/* ======================================================================== */
/* gpio_configure_pin — MODER register                                       */
/* ======================================================================== */

void test_configure_pin_input_clears_moder_bits(void)
{
    /* Start dirty: pre-set pin 5 to output mode */
    fake_GPIOA.MODER = (1U << (5 * 2));
    gpio_configure_pin(GPIO_PORT_A, 5, GPIO_MODE_INPUT);
    /* Input = 00 → both bits of pin 5 must be 0 */
    TEST_ASSERT_EQUAL_HEX32(0U, fake_GPIOA.MODER);
}

void test_configure_pin_output_sets_moder_01(void)
{
    gpio_configure_pin(GPIO_PORT_A, 5, GPIO_MODE_OUTPUT);
    /* Pin 5 bits [11:10] = 01 */
    TEST_ASSERT_EQUAL_HEX32(1U << (5 * 2), fake_GPIOA.MODER);
}

void test_configure_pin_af_sets_moder_10(void)
{
    gpio_configure_pin(GPIO_PORT_A, 5, GPIO_MODE_AF);
    /* Pin 5 bits [11:10] = 10 */
    TEST_ASSERT_EQUAL_HEX32(2U << (5 * 2), fake_GPIOA.MODER);
}

void test_configure_pin_analog_sets_moder_11(void)
{
    gpio_configure_pin(GPIO_PORT_A, 5, GPIO_MODE_ANALOG);
    /* Pin 5 bits [11:10] = 11 */
    TEST_ASSERT_EQUAL_HEX32(3U << (5 * 2), fake_GPIOA.MODER);
}

void test_configure_pin_clears_previous_mode(void)
{
    gpio_configure_pin(GPIO_PORT_A, 5, GPIO_MODE_OUTPUT);
    gpio_configure_pin(GPIO_PORT_A, 5, GPIO_MODE_INPUT);
    TEST_ASSERT_EQUAL_HEX32(0U, fake_GPIOA.MODER);
}

void test_configure_pin_does_not_affect_other_pins(void)
{
    /* Set pin 3 to output, then configure pin 5 */
    gpio_configure_pin(GPIO_PORT_A, 3, GPIO_MODE_OUTPUT);
    gpio_configure_pin(GPIO_PORT_A, 5, GPIO_MODE_AF);
    /* Pin 3 bits [7:6] = 01, pin 5 bits [11:10] = 10 */
    uint32_t expected = (1U << (3 * 2)) | (2U << (5 * 2));
    TEST_ASSERT_EQUAL_HEX32(expected, fake_GPIOA.MODER);
}

void test_configure_pin_invalid_pin_is_noop(void)
{
    gpio_configure_pin(GPIO_PORT_A, 16, GPIO_MODE_OUTPUT);
    TEST_ASSERT_EQUAL_HEX32(0U, fake_GPIOA.MODER);
}

void test_configure_pin_invalid_mode_is_noop(void)
{
    gpio_configure_pin(GPIO_PORT_A, 5, GPIO_MODE_INVALID);
    TEST_ASSERT_EQUAL_HEX32(0U, fake_GPIOA.MODER);
}

/* ======================================================================== */
/* gpio_set_pin / gpio_clear_pin / gpio_toggle_pin                           */
/* ======================================================================== */

void test_set_pin_writes_bsrr_set_bit(void)
{
    gpio_set_pin(GPIO_PORT_A, 5);
    TEST_ASSERT_BITS_HIGH(1U << 5, fake_GPIOA.BSRR);
    /* Clear bit (upper 16) must not be set */
    TEST_ASSERT_BITS_LOW(1U << (5 + 16), fake_GPIOA.BSRR);
}

void test_clear_pin_writes_bsrr_reset_bit(void)
{
    gpio_clear_pin(GPIO_PORT_A, 5);
    TEST_ASSERT_BITS_HIGH(1U << (5 + 16), fake_GPIOA.BSRR);
    TEST_ASSERT_BITS_LOW(1U << 5, fake_GPIOA.BSRR);
}

void test_set_pin_invalid_pin_is_noop(void)
{
    gpio_set_pin(GPIO_PORT_A, 16);
    TEST_ASSERT_EQUAL_HEX32(0U, fake_GPIOA.BSRR);
}

void test_toggle_pin_flips_odr_bit(void)
{
    fake_GPIOA.ODR = 0U;
    gpio_toggle_pin(GPIO_PORT_A, 5);
    TEST_ASSERT_BITS_HIGH(1U << 5, fake_GPIOA.ODR);
    gpio_toggle_pin(GPIO_PORT_A, 5);
    TEST_ASSERT_BITS_LOW(1U << 5, fake_GPIOA.ODR);
}

void test_toggle_pin_does_not_affect_other_bits(void)
{
    fake_GPIOA.ODR = (1U << 3);  /* pin 3 already high */
    gpio_toggle_pin(GPIO_PORT_A, 5);
    TEST_ASSERT_BITS_HIGH(1U << 3, fake_GPIOA.ODR);  /* pin 3 unchanged */
    TEST_ASSERT_BITS_HIGH(1U << 5, fake_GPIOA.ODR);  /* pin 5 toggled */
}

/* ======================================================================== */
/* gpio_read_pin — IDR register                                              */
/* ======================================================================== */

void test_read_pin_returns_1_when_idr_bit_set(void)
{
    fake_GPIOA.IDR = (1U << 5);
    TEST_ASSERT_EQUAL(1, gpio_read_pin(GPIO_PORT_A, 5));
}

void test_read_pin_returns_0_when_idr_bit_clear(void)
{
    fake_GPIOA.IDR = 0U;
    TEST_ASSERT_EQUAL(0, gpio_read_pin(GPIO_PORT_A, 5));
}

void test_read_pin_only_reads_requested_bit(void)
{
    fake_GPIOA.IDR = (1U << 3);  /* pin 3 high, pin 5 low */
    TEST_ASSERT_EQUAL(0, gpio_read_pin(GPIO_PORT_A, 5));
    TEST_ASSERT_EQUAL(1, gpio_read_pin(GPIO_PORT_A, 3));
}

void test_read_pin_invalid_pin_returns_0(void)
{
    fake_GPIOA.IDR = 0xFFFFFFFFU;
    TEST_ASSERT_EQUAL(0, gpio_read_pin(GPIO_PORT_A, 16));
}

/* ======================================================================== */
/* gpio_set_af — AFR registers                                               */
/* ======================================================================== */

void test_set_af_low_pin_uses_afr0(void)
{
    /* Pin 5, AF7: reg=0, pos=5%8*4=20, bits [23:20] = 7 */
    gpio_set_af(GPIO_PORT_A, 5, 7);
    TEST_ASSERT_EQUAL_HEX32(7U << 20, fake_GPIOA.AFR[0]);
    TEST_ASSERT_EQUAL_HEX32(0U,       fake_GPIOA.AFR[1]);
}

void test_set_af_high_pin_uses_afr1(void)
{
    /* Pin 10, AF7: reg=1, pos=10%8*4=8, bits [11:8] = 7 */
    gpio_set_af(GPIO_PORT_A, 10, 7);
    TEST_ASSERT_EQUAL_HEX32(0U,       fake_GPIOA.AFR[0]);
    TEST_ASSERT_EQUAL_HEX32(7U << 8,  fake_GPIOA.AFR[1]);
}

void test_set_af_pin_0_afr0_bits_3_0(void)
{
    gpio_set_af(GPIO_PORT_A, 0, 5);
    TEST_ASSERT_EQUAL_HEX32(5U, fake_GPIOA.AFR[0]);
}

void test_set_af_pin_15_afr1_bits_31_28(void)
{
    gpio_set_af(GPIO_PORT_A, 15, 14);
    TEST_ASSERT_EQUAL_HEX32(14U << 28, fake_GPIOA.AFR[1]);
}

void test_set_af_clears_previous_value(void)
{
    gpio_set_af(GPIO_PORT_A, 5, 5);   /* set AF5 */
    gpio_set_af(GPIO_PORT_A, 5, 7);   /* overwrite with AF7 */
    TEST_ASSERT_EQUAL_HEX32(7U << 20, fake_GPIOA.AFR[0]);
}

void test_set_af_does_not_affect_adjacent_pin(void)
{
    gpio_set_af(GPIO_PORT_A, 4, 3);   /* pin 4: bits [19:16] = 3 */
    gpio_set_af(GPIO_PORT_A, 5, 7);   /* pin 5: bits [23:20] = 7 */
    uint32_t expected = (3U << 16) | (7U << 20);
    TEST_ASSERT_EQUAL_HEX32(expected, fake_GPIOA.AFR[0]);
}

void test_set_af_invalid_pin_is_noop(void)
{
    gpio_set_af(GPIO_PORT_A, 16, 7);
    TEST_ASSERT_EQUAL_HEX32(0U, fake_GPIOA.AFR[0]);
    TEST_ASSERT_EQUAL_HEX32(0U, fake_GPIOA.AFR[1]);
}

void test_set_af_invalid_af_is_noop(void)
{
    gpio_set_af(GPIO_PORT_A, 5, 16);
    TEST_ASSERT_EQUAL_HEX32(0U, fake_GPIOA.AFR[0]);
}

/* ======================================================================== */
/* gpio_set_output_type — OTYPER register                                    */
/* ======================================================================== */

void test_set_output_type_push_pull_clears_bit(void)
{
    fake_GPIOA.OTYPER = (1U << 5);  /* pre-set to open-drain */
    gpio_set_output_type(GPIO_PORT_A, 5, GPIO_OUTPUT_PUSH_PULL);
    TEST_ASSERT_BITS_LOW(1U << 5, fake_GPIOA.OTYPER);
}

void test_set_output_type_open_drain_sets_bit(void)
{
    gpio_set_output_type(GPIO_PORT_A, 5, GPIO_OUTPUT_OPEN_DRAIN);
    TEST_ASSERT_BITS_HIGH(1U << 5, fake_GPIOA.OTYPER);
}

void test_set_output_type_does_not_affect_other_pins(void)
{
    fake_GPIOA.OTYPER = (1U << 3);
    gpio_set_output_type(GPIO_PORT_A, 5, GPIO_OUTPUT_OPEN_DRAIN);
    TEST_ASSERT_BITS_HIGH(1U << 3, fake_GPIOA.OTYPER);
    TEST_ASSERT_BITS_HIGH(1U << 5, fake_GPIOA.OTYPER);
}

/* ======================================================================== */
/* gpio_set_speed — OSPEEDR register                                         */
/* ======================================================================== */

void test_set_speed_low_clears_ospeedr_bits(void)
{
    fake_GPIOA.OSPEEDR = (3U << (5 * 2));  /* pre-set to HIGH */
    gpio_set_speed(GPIO_PORT_A, 5, GPIO_SPEED_LOW);
    TEST_ASSERT_EQUAL_HEX32(0U, fake_GPIOA.OSPEEDR);
}

void test_set_speed_medium_sets_ospeedr_01(void)
{
    gpio_set_speed(GPIO_PORT_A, 5, GPIO_SPEED_MEDIUM);
    TEST_ASSERT_EQUAL_HEX32(1U << (5 * 2), fake_GPIOA.OSPEEDR);
}

void test_set_speed_high_sets_ospeedr_11(void)
{
    gpio_set_speed(GPIO_PORT_A, 5, GPIO_SPEED_HIGH);
    TEST_ASSERT_EQUAL_HEX32(3U << (5 * 2), fake_GPIOA.OSPEEDR);
}

/* ======================================================================== */
/* gpio_set_pull — PUPDR register                                            */
/* ======================================================================== */

void test_set_pull_none_clears_pupdr_bits(void)
{
    fake_GPIOA.PUPDR = (1U << (5 * 2));
    gpio_set_pull(GPIO_PORT_A, 5, GPIO_PULL_NONE);
    TEST_ASSERT_EQUAL_HEX32(0U, fake_GPIOA.PUPDR);
}

void test_set_pull_up_sets_pupdr_01(void)
{
    gpio_set_pull(GPIO_PORT_A, 5, GPIO_PULL_UP);
    TEST_ASSERT_EQUAL_HEX32(1U << (5 * 2), fake_GPIOA.PUPDR);
}

void test_set_pull_down_sets_pupdr_10(void)
{
    gpio_set_pull(GPIO_PORT_A, 5, GPIO_PULL_DOWN);
    TEST_ASSERT_EQUAL_HEX32(2U << (5 * 2), fake_GPIOA.PUPDR);
}

/* ======================================================================== */
/* gpio_configure_full — all registers in one call                           */
/* ======================================================================== */

void test_configure_full_sets_all_four_registers(void)
{
    gpio_configure_full(GPIO_PORT_A, 5,
                        GPIO_MODE_AF,
                        GPIO_OUTPUT_PUSH_PULL,
                        GPIO_SPEED_HIGH,
                        GPIO_PULL_UP);

    TEST_ASSERT_EQUAL_HEX32(2U << (5 * 2), fake_GPIOA.MODER);     /* AF = 10 */
    TEST_ASSERT_BITS_LOW(1U << 5,           fake_GPIOA.OTYPER);    /* push-pull */
    TEST_ASSERT_EQUAL_HEX32(3U << (5 * 2), fake_GPIOA.OSPEEDR);   /* HIGH = 11 */
    TEST_ASSERT_EQUAL_HEX32(1U << (5 * 2), fake_GPIOA.PUPDR);     /* pull-up = 01 */
}

/* ======================================================================== */
/* Port routing — each port maps to its own fake struct                      */
/* ======================================================================== */

void test_configure_port_b_modifies_gpiob_not_gpioa(void)
{
    gpio_configure_pin(GPIO_PORT_B, 3, GPIO_MODE_OUTPUT);
    TEST_ASSERT_EQUAL_HEX32(1U << (3 * 2), fake_GPIOB.MODER);
    TEST_ASSERT_EQUAL_HEX32(0U,             fake_GPIOA.MODER);
}

void test_configure_port_c_modifies_gpioc(void)
{
    gpio_configure_pin(GPIO_PORT_C, 13, GPIO_MODE_INPUT);
    /* Input clears bits — starting from 0, still 0 */
    TEST_ASSERT_EQUAL_HEX32(0U, fake_GPIOC.MODER);
    /* Verify pin 13 can be read back */
    fake_GPIOC.IDR = (1U << 13);
    TEST_ASSERT_EQUAL(1, gpio_read_pin(GPIO_PORT_C, 13));
}

void test_set_pin_port_h_modifies_gpioh(void)
{
    gpio_set_pin(GPIO_PORT_H, 0);
    TEST_ASSERT_BITS_HIGH(1U << 0, fake_GPIOH.BSRR);
    TEST_ASSERT_EQUAL_HEX32(0U,    fake_GPIOA.BSRR);
}

/* ======================================================================== */
/* main                                                                      */
/* ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* Clock enable / disable */
    RUN_TEST(test_clock_enable_port_a_sets_gpioaen);
    RUN_TEST(test_clock_enable_port_b_sets_gpioben);
    RUN_TEST(test_clock_enable_port_c_sets_gpiocen);
    RUN_TEST(test_clock_enable_does_not_clear_other_bits);
    RUN_TEST(test_clock_disable_port_a_clears_gpioaen);
    RUN_TEST(test_clock_disable_does_not_clear_other_bits);

    /* gpio_configure_pin — MODER */
    RUN_TEST(test_configure_pin_input_clears_moder_bits);
    RUN_TEST(test_configure_pin_output_sets_moder_01);
    RUN_TEST(test_configure_pin_af_sets_moder_10);
    RUN_TEST(test_configure_pin_analog_sets_moder_11);
    RUN_TEST(test_configure_pin_clears_previous_mode);
    RUN_TEST(test_configure_pin_does_not_affect_other_pins);
    RUN_TEST(test_configure_pin_invalid_pin_is_noop);
    RUN_TEST(test_configure_pin_invalid_mode_is_noop);

    /* gpio_set/clear/toggle */
    RUN_TEST(test_set_pin_writes_bsrr_set_bit);
    RUN_TEST(test_clear_pin_writes_bsrr_reset_bit);
    RUN_TEST(test_set_pin_invalid_pin_is_noop);
    RUN_TEST(test_toggle_pin_flips_odr_bit);
    RUN_TEST(test_toggle_pin_does_not_affect_other_bits);

    /* gpio_read_pin */
    RUN_TEST(test_read_pin_returns_1_when_idr_bit_set);
    RUN_TEST(test_read_pin_returns_0_when_idr_bit_clear);
    RUN_TEST(test_read_pin_only_reads_requested_bit);
    RUN_TEST(test_read_pin_invalid_pin_returns_0);

    /* gpio_set_af */
    RUN_TEST(test_set_af_low_pin_uses_afr0);
    RUN_TEST(test_set_af_high_pin_uses_afr1);
    RUN_TEST(test_set_af_pin_0_afr0_bits_3_0);
    RUN_TEST(test_set_af_pin_15_afr1_bits_31_28);
    RUN_TEST(test_set_af_clears_previous_value);
    RUN_TEST(test_set_af_does_not_affect_adjacent_pin);
    RUN_TEST(test_set_af_invalid_pin_is_noop);
    RUN_TEST(test_set_af_invalid_af_is_noop);

    /* gpio_set_output_type */
    RUN_TEST(test_set_output_type_push_pull_clears_bit);
    RUN_TEST(test_set_output_type_open_drain_sets_bit);
    RUN_TEST(test_set_output_type_does_not_affect_other_pins);

    /* gpio_set_speed */
    RUN_TEST(test_set_speed_low_clears_ospeedr_bits);
    RUN_TEST(test_set_speed_medium_sets_ospeedr_01);
    RUN_TEST(test_set_speed_high_sets_ospeedr_11);

    /* gpio_set_pull */
    RUN_TEST(test_set_pull_none_clears_pupdr_bits);
    RUN_TEST(test_set_pull_up_sets_pupdr_01);
    RUN_TEST(test_set_pull_down_sets_pupdr_10);

    /* gpio_configure_full */
    RUN_TEST(test_configure_full_sets_all_four_registers);

    /* Port routing */
    RUN_TEST(test_configure_port_b_modifies_gpiob_not_gpioa);
    RUN_TEST(test_configure_port_c_modifies_gpioc);
    RUN_TEST(test_set_pin_port_h_modifies_gpioh);

    return UNITY_END();
}
