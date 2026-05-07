#include "unity.h"
#include "stm32f4xx.h"
#include "test_periph.h"
#include "sleep_mode.h"

void setUp(void) {
    test_periph_reset();
}

void tearDown(void) {}

/* --- sleep_mode_init tests --- */

void test_init_enables_pwr_clock(void) {
    sleep_mode_init();
    TEST_ASSERT_BITS_HIGH(RCC_APB1ENR_PWREN, fake_RCC.APB1ENR);
}

void test_init_clears_sleepdeep(void) {
    fake_SCB.SCR = SCB_SCR_SLEEPDEEP_Msk;
    sleep_mode_init();
    TEST_ASSERT_BITS_LOW(SCB_SCR_SLEEPDEEP_Msk, fake_SCB.SCR);
}

/* --- enter_sleep_mode tests --- */

void test_sleep_clears_sleeponexit(void) {
    fake_SCB.SCR = SCB_SCR_SLEEPONEXIT_Msk;
    enter_sleep_mode();
    TEST_ASSERT_BITS_LOW(SCB_SCR_SLEEPONEXIT_Msk, fake_SCB.SCR);
}

/* --- enter_stop_mode tests --- */

void test_stop_sets_cwuf(void) {
    enter_stop_mode();
    TEST_ASSERT_BITS_HIGH(PWR_CR_CWUF, fake_PWR.CR);
}

void test_stop_sets_lpds(void) {
    enter_stop_mode();
    TEST_ASSERT_BITS_HIGH(PWR_CR_LPDS, fake_PWR.CR);
}

void test_stop_sets_fpds(void) {
    enter_stop_mode();
    TEST_ASSERT_BITS_HIGH(PWR_CR_FPDS, fake_PWR.CR);
}

void test_stop_clears_pdds(void) {
    fake_PWR.CR = PWR_CR_PDDS;
    enter_stop_mode();
    TEST_ASSERT_BITS_LOW(PWR_CR_PDDS, fake_PWR.CR);
}

void test_stop_clears_sleepdeep_on_return(void) {
    enter_stop_mode();
    TEST_ASSERT_BITS_LOW(SCB_SCR_SLEEPDEEP_Msk, fake_SCB.SCR);
}

void test_stop_does_not_set_pdds(void) {
    enter_stop_mode();
    TEST_ASSERT_BITS_LOW(PWR_CR_PDDS, fake_PWR.CR);
}

/* --- enter_standby_mode tests --- */

void test_standby_sets_pdds(void) {
    enter_standby_mode(0);
    TEST_ASSERT_BITS_HIGH(PWR_CR_PDDS, fake_PWR.CR);
}

void test_standby_sets_cwuf(void) {
    enter_standby_mode(0);
    TEST_ASSERT_BITS_HIGH(PWR_CR_CWUF, fake_PWR.CR);
}

void test_standby_sets_csbf(void) {
    enter_standby_mode(0);
    TEST_ASSERT_BITS_HIGH(PWR_CR_CSBF, fake_PWR.CR);
}

void test_standby_sets_sleepdeep(void) {
    enter_standby_mode(0);
    TEST_ASSERT_BITS_HIGH(SCB_SCR_SLEEPDEEP_Msk, fake_SCB.SCR);
}

void test_standby_enables_wkup_pin(void) {
    enter_standby_mode(1);
    TEST_ASSERT_BITS_HIGH(PWR_CSR_EWUP, fake_PWR.CSR);
}

void test_standby_does_not_enable_wkup_pin_when_zero(void) {
    enter_standby_mode(0);
    TEST_ASSERT_BITS_LOW(PWR_CSR_EWUP, fake_PWR.CSR);
}

/* --- sleep_was_standby_wakeup tests --- */

void test_was_standby_returns_1_when_sbf_set(void) {
    fake_PWR.CSR = PWR_CSR_SBF;
    TEST_ASSERT_EQUAL(1, sleep_was_standby_wakeup());
}

void test_was_standby_returns_0_when_sbf_clear(void) {
    fake_PWR.CSR = 0;
    TEST_ASSERT_EQUAL(0, sleep_was_standby_wakeup());
}

/* --- sleep_clear_standby_flag tests --- */

void test_clear_standby_flag_sets_csbf(void) {
    fake_PWR.CR = 0;
    sleep_clear_standby_flag();
    TEST_ASSERT_BITS_HIGH(PWR_CR_CSBF, fake_PWR.CR);
}

/* --- sleep_clear_wakeup_flag tests --- */

void test_clear_wakeup_flag_sets_cwuf(void) {
    fake_PWR.CR = 0;
    sleep_clear_wakeup_flag();
    TEST_ASSERT_BITS_HIGH(PWR_CR_CWUF, fake_PWR.CR);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_init_enables_pwr_clock);
    RUN_TEST(test_init_clears_sleepdeep);

    RUN_TEST(test_sleep_clears_sleeponexit);

    RUN_TEST(test_stop_sets_cwuf);
    RUN_TEST(test_stop_sets_lpds);
    RUN_TEST(test_stop_sets_fpds);
    RUN_TEST(test_stop_clears_pdds);
    RUN_TEST(test_stop_clears_sleepdeep_on_return);
    RUN_TEST(test_stop_does_not_set_pdds);

    RUN_TEST(test_standby_sets_pdds);
    RUN_TEST(test_standby_sets_cwuf);
    RUN_TEST(test_standby_sets_csbf);
    RUN_TEST(test_standby_sets_sleepdeep);
    RUN_TEST(test_standby_enables_wkup_pin);
    RUN_TEST(test_standby_does_not_enable_wkup_pin_when_zero);

    RUN_TEST(test_was_standby_returns_1_when_sbf_set);
    RUN_TEST(test_was_standby_returns_0_when_sbf_clear);

    RUN_TEST(test_clear_standby_flag_sets_csbf);
    RUN_TEST(test_clear_wakeup_flag_sets_cwuf);

    return UNITY_END();
}
