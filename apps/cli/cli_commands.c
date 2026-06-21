#include "cli_commands.h"
#include "crc.h"
#include "fault_handler.h"
#include "flash.h"
#include "led2.h"
#include "printf.h"
#include "printf_dma.h"
#include "rcc.h"
#include "rtc_backup.h"
#include "sleep_mode.h"
#include "spi_perf.h"
#include "stm32f4xx.h"
#include "systick.h"
#include "timer.h"
#include "uart.h"

#ifdef HIL_TEST_MODE
#include "test_output.h"
#endif

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

/* ---- run_all_tests command (HIL test mode only) ------------------------ */

#ifdef HIL_TEST_MODE
/**
 * @brief Run all hardware-in-the-loop tests
 *
 * Executes the complete HIL test suite:
 * 1. Unity test cases (from test_harness.c)
 * 2. Performance benchmarks (SPI throughput)
 *
 * Emits machine-parseable output for automation via START_TESTS/END_TESTS
 * markers and TEST:name:PASS/FAIL:metrics lines.
 *
 * This command is only available when compiled with HIL_TEST=1.
 */
static int cmd_run_all_tests(const char* args) {
    (void)args;
    
    printf("\n");
    printf("========================================\n");
    printf("  HIL Test Suite\n");
    printf("========================================\n");
    
    TEST_OUTPUT_START();
    printf_dma_flush();
    
    /* Run all Unity tests (SPI sweep + FPU) from test_harness.c */
    run_unity_tests();
    
    TEST_OUTPUT_END();
    printf_dma_flush();
    
    printf("\n");
    printf("========================================\n");
    printf("  All tests complete\n");
    printf("========================================\n");
    printf_dma_flush();
    
    return 0;
}
#endif /* HIL_TEST_MODE */

static int cmd_uptime(const char* args) {
    (void)args;
    uint32_t ms = systick_get_ms();
    uint32_t s  = ms / 1000U;
    uint32_t h  = s / 3600U; s %= 3600U;
    uint32_t m  = s / 60U;   s %= 60U;
    printf("%02lu:%02lu:%02lu.%03lu\n",
           (unsigned long)h, (unsigned long)m,
           (unsigned long)s, (unsigned long)(ms % 1000U));
    return 0;
}

/* ---- Flash commands ------------------------------------------------------- */

static uint32_t parse_hex_or_dec(const char *s, const char **end)
{
    uint32_t val = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        while ((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f') ||
               (*s >= 'A' && *s <= 'F')) {
            val <<= 4;
            if (*s >= '0' && *s <= '9')      val |= (uint32_t)(*s - '0');
            else if (*s >= 'a' && *s <= 'f') val |= (uint32_t)(*s - 'a' + 10);
            else                             val |= (uint32_t)(*s - 'A' + 10);
            s++;
        }
    } else {
        while (*s >= '0' && *s <= '9') {
            val = val * 10 + (uint32_t)(*s - '0');
            s++;
        }
    }
    if (end) *end = s;
    return val;
}

static int cmd_flash_read(const char *args)
{
    while (args && *args == ' ') args++;
    if (!args || !*args) {
        printf("Usage: flash_read <addr> [count]\n");
        printf("  addr:  hex or decimal flash address\n");
        printf("  count: number of 32-bit words (default: 1, max: 64)\n");
        return 1;
    }

    const char *p;
    uint32_t addr = parse_hex_or_dec(args, &p);

    while (*p == ' ') p++;
    uint32_t count = 1;
    if (*p) count = parse_hex_or_dec(p, NULL);
    if (count == 0) count = 1;
    if (count > 64) count = 64;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t a = addr + i * 4;
        uint32_t val;
        err_t ret = flash_read_word(a, &val);
        if (ret != ERR_OK) {
            printf("Error reading 0x%08lX\n", (unsigned long)a);
            return 1;
        }
        if ((i % 4) == 0) printf("0x%08lX: ", (unsigned long)a);
        printf("%08lX ", (unsigned long)val);
        if ((i % 4) == 3 || i == count - 1) printf("\n");
    }
    return 0;
}

static int cmd_flash_write(const char *args)
{
    while (args && *args == ' ') args++;
    if (!args || !*args) {
        printf("Usage: flash_write <addr> <value>\n");
        printf("  addr:  4-byte aligned flash address\n");
        printf("  value: 32-bit word to program\n");
        return 1;
    }

    const char *p;
    uint32_t addr = parse_hex_or_dec(args, &p);
    while (*p == ' ') p++;
    if (!*p) {
        printf("Missing value argument\n");
        return 1;
    }
    uint32_t val = parse_hex_or_dec(p, NULL);

    /* Safety: prevent writing to sectors 0-3 (application code region) */
    if (addr < 0x08010000U) {
        printf("ERROR: writing to code region (sectors 0-3) is not allowed\n");
        return 1;
    }

    err_t ret = flash_unlock();
    if (ret != ERR_OK) {
        printf("Flash unlock failed\n");
        return 1;
    }

    ret = flash_write_word(addr, val);
    flash_lock();

    if (ret != ERR_OK) {
        printf("Write failed (err %d)\n", ret);
        return 1;
    }
    printf("Wrote 0x%08lX to 0x%08lX\n", (unsigned long)val, (unsigned long)addr);
    return 0;
}

static int cmd_flash_erase(const char *args)
{
    while (args && *args == ' ') args++;
    if (!args || !*args) {
        printf("Usage: flash_erase <sector>\n");
        printf("  sector: 0-7 (sector 0 is protected)\n");
        return 1;
    }

    uint32_t sector = parse_hex_or_dec(args, NULL);

    /* Safety: prevent erasing sectors 0-3 (application code region) */
    if (sector <= 3) {
        printf("ERROR: erasing sectors 0-3 (code region) is not allowed\n");
        return 1;
    }
    if (sector > FLASH_SECTOR_MAX) {
        printf("Invalid sector %lu (max %u)\n",
               (unsigned long)sector, FLASH_SECTOR_MAX);
        return 1;
    }

    err_t ret = flash_unlock();
    if (ret != ERR_OK) {
        printf("Flash unlock failed\n");
        return 1;
    }

    printf("Erasing sector %lu (0x%08lX, %lu KB)...\n",
           (unsigned long)sector,
           (unsigned long)flash_get_sector_address((uint8_t)sector),
           (unsigned long)(flash_get_sector_size((uint8_t)sector) / 1024U));

    ret = flash_erase_sector((uint8_t)sector);
    flash_lock();

    if (ret != ERR_OK) {
        printf("Erase failed (err %d)\n", ret);
        return 1;
    }
    printf("Erase complete\n");
    return 0;
}

static int cmd_crc_test(const char *args)
{
    while (args && *args == ' ') args++;

    uint32_t addr = 0x08000000U;
    uint32_t words = 256;

    if (args && *args) {
        const char *p;
        addr = parse_hex_or_dec(args, &p);
        while (*p == ' ') p++;
        if (*p) words = parse_hex_or_dec(p, NULL);
    }

    if (words == 0) words = 1;
    if (words > 16384) words = 16384;

    /* Align addr to 4 bytes */
    addr &= ~3U;

    crc_init();
    crc_reset();
    uint32_t result = crc_accumulate((const uint32_t *)addr, words);

    printf("CRC32 of %lu words at 0x%08lX: 0x%08lX\n",
           (unsigned long)words, (unsigned long)addr, (unsigned long)result);
    return 0;
}

static int cmd_stop_mode(const char *args)
{
    (void)args;
    printf("Entering Stop mode (low-power regulator, flash off)...\n");
    printf_dma_flush();

    enter_stop_mode();

    /* Woke up — HSI is now the clock source. Restore PLL to 100 MHz. */
    rcc_init(RCC_CLK_SRC_HSI, 100000000U);
    uart_init();
    systick_init();
    printf_dma_init();

    printf("Woke from Stop mode — clock restored to 100 MHz\n");
    return 0;
}

static int cmd_standby_mode(const char *args)
{
    (void)args;
    printf("Entering Standby mode (WKUP pin enabled)...\n");
    printf("System will reset on wakeup.\n");
    printf_dma_flush();

    enter_standby_mode(1);

    /* Should never reach here on real hardware */
    return 0;
}

/*
 * Plan 001 Phase 1.8 — request the bootloader to enter OTA mode on the
 * next reset.  Writes the OTA magic into RTC_BKP_DR0, drains pending
 * UART output, and triggers NVIC_SystemReset().  The bootloader sees the
 * magic at its earliest startup, clears it, and runs bootloader_ota_run().
 *
 * Backup registers survive a CPU reset (and a brief power loss while VDD
 * is held up by the chip's bypass caps), so the magic reliably reaches
 * the bootloader.  See docs/wiki/plans/001-bootloader/ota.md.
 */
static int cmd_ota_request(const char *args)
{
    (void)args;
    printf("Entering OTA mode on next reset (writing magic to RTC_BKP_DR0)\n");
    printf_dma_flush();

    rtc_backup_enable_writes();
    rtc_backup_write_dr0(RTC_BACKUP_OTA_MAGIC);

    /* Tiny dwell so the final UART byte makes it onto the wire before
     * the reset clears the TX shift register. */
    for (volatile uint32_t i = 0; i < 100000u; ++i) {
        __asm volatile ("nop");
    }
    NVIC_SystemReset();
    /* unreachable */
    return 0;
}

/*
 * Software system reset.  Drains pending UART output, then triggers
 * NVIC_SystemReset() so the chip reboots through the bootloader chain.
 * Unlike ota_request it writes no RTC magic, so the bootloader performs a
 * normal slot selection rather than entering OTA mode.
 */
static int cmd_reset(const char *args)
{
    (void)args;
    printf("Resetting...\n");
    printf_dma_flush();

    /* Tiny dwell so the final UART byte makes it onto the wire before
     * the reset clears the TX shift register. */
    for (volatile uint32_t i = 0; i < 100000u; ++i) {
        __asm volatile ("nop");
    }
    NVIC_SystemReset();
    /* unreachable */
    return 0;
}

// Command table (help command is automatically added by CLI library)
static const cli_command_t commands[] = {
    {"uptime",        "Print uptime (hh:mm:ss.mmm)", cmd_uptime},
    {"led_on",        "Turn on LED2",              cmd_led_on},
    {"led_off",       "Turn off LED2",             cmd_led_off},
    {"led_toggle",    "Toggle LED2 state",         cmd_led_toggle},
    {"led_blink",     "Blink LED2 <count> <interval_ms>", cmd_led_blink},
    {"spi_perf_test", "SPI master TX perf test",   cmd_spi_perf_test},
    {"flash_read",    "Read flash <addr> [count]",        cmd_flash_read},
    {"flash_write",   "Write flash <addr> <value>",       cmd_flash_write},
    {"flash_erase",   "Erase flash <sector> (4-7)",       cmd_flash_erase},
    {"crc_test",      "CRC32 of flash <addr> [words]",    cmd_crc_test},
    {"stop_mode",     "Enter Stop mode (wake on interrupt)",      cmd_stop_mode},
    {"standby_mode",  "Enter Standby mode (full reset on wake)",  cmd_standby_mode},
    {"ota_request",   "Reboot into bootloader OTA mode",          cmd_ota_request},
    {"reset",         "Software system reset (reboot)",           cmd_reset},
    {"fault_test",    "Trigger a fault (nullptr|divzero|illegal)", cmd_fault_test},
#ifdef ENABLE_HW_FPU
    {"fpu_test",      "Validate HW FPU is working", cmd_fpu_test},
#endif
#ifdef HIL_TEST_MODE
    {"run_all_tests", "Execute all HIL tests", cmd_run_all_tests},
#endif
};

const cli_command_t* cli_commands_get_table(size_t* num_commands) {
    *num_commands = sizeof(commands) / sizeof(commands[0]);
    return commands;
}

