#include "cli_commands.h"
#include "fault_handler.h"
#include "led2.h"
#include "printf.h"
#include "spi_perf.h"

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

// Command table (help command is automatically added by CLI library)
static const cli_command_t commands[] = {
    {"led_on",        "Turn on LED2",              cmd_led_on},
    {"led_off",       "Turn off LED2",             cmd_led_off},
    {"led_toggle",    "Toggle LED2 state",         cmd_led_toggle},
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

