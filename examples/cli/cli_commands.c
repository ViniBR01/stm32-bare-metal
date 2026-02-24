#include "cli_commands.h"
#include "fault_handler.h"
#include "led2.h"
#include "printf.h"
#include "rcc.h"
#include "spi_perf.h"
#include "timer.h"

// Command implementations
// NOTE: These are called from main loop context (not ISR),
// so we can safely use printf() which accumulates to buffer

#ifdef ENABLE_HW_FPU
/**
 * @brief Validate that the hardware FPU is operational.
 *
 * Performs a small set of single-precision floating-point operations
 * (multiply, divide, add) and prints the results.  If the FPU was not
 * enabled in Reset_Handler (SCB->CPACR), this would trigger a UsageFault.
 */
static int cmd_fpu_test(const char* args) {
    (void)args;

    volatile float a = 3.14f;
    volatile float b = 2.72f;
    volatile float mul = a * b;
    volatile float div = a / b;
    volatile float add = a + b;

    /* Print integer and fractional parts (printf may not support %f on
     * minimal implementations, so we split manually). */
    printf("FPU test  (a = 3.14, b = 2.72)\n");

    int mul_i = (int)mul;
    int mul_f = (int)((mul - (float)mul_i) * 1000.0f);
    if (mul_f < 0) mul_f = -mul_f;
    printf("  a * b = %d.%03d\n", mul_i, mul_f);

    int div_i = (int)div;
    int div_f = (int)((div - (float)div_i) * 1000.0f);
    if (div_f < 0) div_f = -div_f;
    printf("  a / b = %d.%03d\n", div_i, div_f);

    int add_i = (int)add;
    int add_f = (int)((add - (float)add_i) * 1000.0f);
    if (add_f < 0) add_f = -add_f;
    printf("  a + b = %d.%03d\n", add_i, add_f);

    printf("FPU OK – no UsageFault\n");
    return 0;
}
#endif /* ENABLE_HW_FPU */

static int cmd_led_on(const char* args) {
    (void)args;
    led2_on();
    printf("LED2 turned on\n");
    return 0;  // Success
}

static int cmd_led_off(const char* args) {
    (void)args;
    led2_off();
    printf("LED2 turned off\n");
    return 0;  // Success
}

static int cmd_led_toggle(const char* args) {
    (void)args;
    led2_toggle();
    printf("LED2 toggled\n");
    return 0;  // Success
}

static int cmd_spi_perf_test(const char* args) {
    spi_perf_args_t cfg = spi_perf_parse_args(args);
    if (cfg.error) {
        printf("Usage: spi_perf_test [spi_num] [prescaler] [buffer_size] [dma]\n");
        printf("  spi_num:     1-5 (default: 2)\n");
        printf("  prescaler:   2, 4, 8, 16, 32, 64, 128, 256 (default: 4)\n");
        printf("  buffer_size: 1-%u (default: %u)\n",
               SPI_PERF_MAX_BUF_SIZE, SPI_PERF_DEFAULT_BUF_SIZE);
        printf("  dma:         optional keyword to use DMA transfer mode\n");
        return 1;
    }
    return spi_perf_run(cfg.instance, cfg.prescaler, cfg.buffer_size, cfg.use_dma);
}

/**
 * @brief Deliberately trigger a HardFault to test the fault handler.
 *
 * Supports several fault types via an optional argument:
 *   fault_test          - bad-address read (default)
 *   fault_test nullptr  - bad-address read
 *   fault_test divzero  - integer divide by zero (requires fault_handler_init)
 *   fault_test illegal  - undefined instruction (permanently undefined encoding)
 *
 * After issuing the command the board should print a register dump over
 * UART and blink LED2 in an SOS pattern.  The faulting PC in the dump
 * can be decoded with arm-none-eabi-addr2line or the .map file.
 */
static int cmd_fault_test(const char* args) {
    if (args == NULL || args[0] == '\0' || args[0] == 'n') {
        /* Read from an unmapped address region (0xBFFFFFFC is between SRAM
         * and peripheral space -- no device exists there on STM32F411).
         * This reliably triggers a BusFault / HardFault. */
        printf("Triggering bad-address read...\n");
        volatile uint32_t val = *(volatile uint32_t *)0xBFFFFFFFU;
        (void)val;
    } else if (args[0] == 'd') {
        /* Integer divide by zero -- requires SCB->CCR DIV_0_TRP enabled
         * (call fault_handler_init() at startup). */
        printf("Triggering divide-by-zero...\n");
        volatile int zero = 0;
        volatile int result = 1 / zero;
        (void)result;
    } else if (args[0] == 'i') {
        /* Execute an undefined instruction (ARM permanently undefined).
         * 0xF7F0A000 is a guaranteed UDF encoding on Thumb-2. */
        printf("Triggering illegal instruction...\n");
        __asm volatile (".short 0xDE00");  /* UDF #0 (Thumb encoding) */
    } else {
        printf("Unknown fault type '%s'\n", args);
        printf("Usage: fault_test [nullptr|divzero|illegal]\n");
        return 1;
    }

    /* Should never reach here */
    return 0;
}

/* ---- led_blink command ------------------------------------------------- */

/**
 * @brief Simple string-to-unsigned-int parser (local helper).
 * @return Pointer past the parsed digits, or NULL on error.
 */
static const char* parse_uint(const char* s, uint32_t* out) {
    if (*s < '0' || *s > '9') return 0;
    uint32_t val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (uint32_t)(*s - '0');
        s++;
    }
    *out = val;
    return s;
}

/* State shared between cmd_led_blink() and the TIM3 ISR callback */
static volatile uint32_t blink_remaining;  /* toggles left (2 * count) */

static void blink_timer_cb(void) {
    led2_toggle();
    if (blink_remaining > 0) {
        blink_remaining--;
    }
    if (blink_remaining == 0) {
        timer_stop(TIMER_3);
        timer_register_callback(TIMER_3, (timer_callback_t)0);
    }
}

/**
 * @brief CLI command: led_blink <count> <interval_ms>
 *
 * Blinks LED2 <count> times with <interval_ms> between each toggle.
 * Uses TIM3 in interrupt mode so the command returns immediately.
 */
static int cmd_led_blink(const char* args) {
    /* Skip leading whitespace */
    while (args && *args == ' ') args++;

    if (args == NULL || *args == '\0') {
        printf("Usage: led_blink <count> <interval_ms>\n");
        return 1;
    }

    /* Parse count */
    uint32_t count = 0;
    const char* p = parse_uint(args, &count);
    if (!p || count == 0) {
        printf("Invalid count\n");
        return 1;
    }

    /* Skip whitespace */
    while (*p == ' ') p++;

    /* Parse interval */
    uint32_t interval_ms = 0;
    p = parse_uint(p, &interval_ms);
    if (!p || interval_ms == 0) {
        printf("Invalid interval_ms\n");
        return 1;
    }

    /* Each blink = on-toggle + off-toggle, so total toggles = 2 * count */
    blink_remaining = count * 2;

    /* Configure TIM3 to fire every interval_ms milliseconds.
     * tick_hz  = timer_clk / (PSC+1)  -- we pick 10 000 Hz (0.1 ms resolution)
     * period   = tick_hz * interval_ms / 1000  */
    uint32_t timer_clk = rcc_get_apb1_timer_clk();
    uint32_t tick_hz = 10000U;
    uint32_t psc = (timer_clk / tick_hz) - 1;
    uint32_t arr = (tick_hz * interval_ms) / 1000U - 1;

    timer_init(TIMER_3, psc, arr);
    timer_register_callback(TIMER_3, blink_timer_cb);
    timer_start(TIMER_3);

    printf("Blinking LED2 %lu times every %lu ms\n",
           (unsigned long)count, (unsigned long)interval_ms);
    return 0;
}

// Command table (help command is automatically added by CLI library)
static const cli_command_t commands[] = {
    {"led_on",        "Turn on LED2",              cmd_led_on},
    {"led_off",       "Turn off LED2",             cmd_led_off},
    {"led_toggle",    "Toggle LED2 state",         cmd_led_toggle},
    {"led_blink",     "Blink LED2 <count> <interval_ms>", cmd_led_blink},
    {"spi_perf_test", "SPI master TX perf test",   cmd_spi_perf_test},
    {"fault_test",    "Trigger a fault (nullptr|divzero|illegal)", cmd_fault_test},
#ifdef ENABLE_HW_FPU
    {"fpu_test",      "Validate HW FPU is working", cmd_fpu_test},
#endif
};

const cli_command_t* cli_commands_get_table(size_t* num_commands) {
    *num_commands = sizeof(commands) / sizeof(commands[0]);
    return commands;
}

