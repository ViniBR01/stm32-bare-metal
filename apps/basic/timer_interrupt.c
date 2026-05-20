#include "timer.h"
#include "led2.h"
#include "rcc.h"

#define TICK_HZ     10000U
#define PERIOD_HZ       2U

static volatile uint32_t g_toggle_led;

static void on_timer_update(void) {
    g_toggle_led = 1;
}

int main(void) {
    led2_init();
    g_toggle_led = 1;

    uint32_t timer_clk = rcc_get_apb1_timer_clk();
    timer_init(TIMER_2, (timer_clk / TICK_HZ) - 1, (TICK_HZ / PERIOD_HZ) - 1);
    timer_register_callback(TIMER_2, on_timer_update);
    timer_start(TIMER_2);

    while (1) {
        if (g_toggle_led) {
            led2_toggle();
            g_toggle_led = 0;
        }
    }

    return 0;
}
