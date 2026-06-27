/*
 * modem_sim — interactive software BPSK modem simulator (Plan 002 B0.3).
 *
 * Runs the self-contained software modem on the NUCLEO-F411RE: a PRBS bit
 * source is BPSK-mapped to q15 symbols, passed through the software AWGN
 * channel, sliced, and checked against the same PRBS to count bit errors.
 * The whole transmit -> channel -> receive chain lives in SRAM; there is no
 * analog fixture and no second board. See
 * docs/wiki/plans/002-dsp-baseband/software-modem.md.
 *
 * CLI:
 *   modem run [--mod bpsk] [--snr <dB>] [--bits <N>]
 *       One BER measurement at a fixed Eb/N0; prints bits, errors, measured
 *       BER, closed-form theory BER, total cycles / Mcycles, and cycles/bit.
 *   modem sweep --snr <lo>:<hi>:<step> [--bits <N>]
 *       An ASCII BER-vs-Eb/N0 table, one row per SNR point.
 *
 * Cycle counts come from the Cortex-M4 DWT cycle counter (same pattern as
 * drivers/src/spi_perf.c); the core runs at rcc_get_sysclk() (100 MHz).
 */

#include <stddef.h>
#include <stdint.h>

#include "stm32f4xx.h"

#include "bl_handshake.h"
#include "cli.h"
#include "fault_handler.h"
#include "flash_slot.h"
#include "led2.h"
#include "printf.h"
#include "printf_dma.h"
#include "rcc.h"
#include "sleep_mode.h"
#include "systick.h"
#include "uart.h"

/* Software modem core (Plan 002 B0.1/B0.2). */
#include "awgn.h"
#include "bpsk.h"
#include "fixed.h"
#include "prbs.h"

/* "modem run --snr 5.5 --bits 1000000" is ~34 chars; 64 leaves headroom. */
#define MODEM_CMD_SIZE 64

/* Defaults chosen so a bare `modem run` reproduces the issue's example. */
#define MODEM_DEFAULT_SNR_DB   6.0f
#define MODEM_DEFAULT_RUN_BITS 100000u
#define MODEM_DEFAULT_SWEEP_BITS 50000u
#define MODEM_SEED             1u
#define MODEM_POLY             PRBS9

static cli_context_t g_cli;
static char g_cmd_buffer[MODEM_CMD_SIZE];
static volatile uint8_t command_pending = 0;

/* ------------------------------------------------------------------ */
/* Argument parsing helpers (no atof/scanf in this codebase).         */
/* ------------------------------------------------------------------ */

/* Skip leading spaces; returns the first non-space character. */
static const char* skip_ws(const char* s) {
    while (*s == ' ') {
        s++;
    }
    return s;
}

/*
 * Parse an unsigned decimal integer. Returns a pointer past the digits, or
 * NULL if no digit is present. (Mirrors parse_uint in apps/cli/cli_commands.c.)
 */
static const char* parse_uint(const char* s, uint32_t* out) {
    if (*s < '0' || *s > '9') {
        return NULL;
    }
    uint32_t val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10u + (uint32_t)(*s - '0');
        s++;
    }
    *out = val;
    return s;
}

/*
 * Parse a decimal number into a float: optional sign, integer part, optional
 * fractional part ('.' followed by digits). Returns a pointer past the number,
 * or NULL if no digit is present. No exponent form — dB values don't need it.
 */
static const char* parse_float(const char* s, float* out) {
    float sign = 1.0f;
    if (*s == '-') {
        sign = -1.0f;
        s++;
    } else if (*s == '+') {
        s++;
    }

    if ((*s < '0' || *s > '9') && *s != '.') {
        return NULL;
    }

    float val = 0.0f;
    int saw_digit = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10.0f + (float)(*s - '0');
        s++;
        saw_digit = 1;
    }
    if (*s == '.') {
        s++;
        float scale = 0.1f;
        while (*s >= '0' && *s <= '9') {
            val += (float)(*s - '0') * scale;
            scale *= 0.1f;
            s++;
            saw_digit = 1;
        }
    }
    if (!saw_digit) {
        return NULL;
    }

    *out = sign * val;
    return s;
}

/*
 * Find the value following a "--<key>" flag in an argument string. On a match,
 * returns a pointer to the first non-space character after the flag; otherwise
 * NULL. Matching is whitespace-delimited so "--snr" does not match "--snrx".
 */
static const char* find_flag(const char* args, const char* key) {
    size_t klen = 0;
    while (key[klen] != '\0') {
        klen++;
    }
    const char* s = args;
    while (*s != '\0') {
        s = skip_ws(s);
        if (*s == '\0') {
            break;
        }
        /* Does the token at s start with key and end at a space/EOL? */
        size_t i = 0;
        while (i < klen && s[i] == key[i]) {
            i++;
        }
        if (i == klen && (s[klen] == '\0' || s[klen] == ' ')) {
            return skip_ws(s + klen);
        }
        /* Advance to the next whitespace-delimited token. */
        while (*s != '\0' && *s != ' ') {
            s++;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Modem chain                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    uint64_t bits;
    uint64_t errors;
    double   theory;
    uint32_t mod_cycles;      /* bit -> symbol (BPSK map)                 */
    uint32_t channel_cycles;  /* symbol -> noisy symbol (AWGN)            */
    uint32_t demod_cycles;    /* noisy symbol -> bit + BER (slice+check)  */
} modem_result_t;

/*
 * Storing every symbol for a 100k-bit run would need ~200 KB (> 128 KB SRAM),
 * so the chain runs block-by-block: each block fills a buffer (mod), adds noise
 * over the whole buffer (channel), then slices+checks it (demod).  The three
 * stages are timed separately and accumulated across blocks, so the reported
 * mod/channel/demod cycles isolate each component instead of one end-to-end
 * number.  Block processing also matches how channel_awgn_apply() is meant to
 * be used (one sigma/powf per block, not per bit).
 */
#define MODEM_BLOCK 1024u

static q15_t g_sym_block[MODEM_BLOCK];

/* Read the DWT cycle counter (enabled once in modem_run_chain). */
static inline uint32_t dwt_now(void) {
    return DWT->CYCCNT;
}

/*
 * Run the PRBS -> BPSK -> AWGN -> slice -> BER chain for nbits, timing the
 * modulation, channel, and demodulation stages separately.  Transmitter and
 * checker share the same polynomial and seed, so they start aligned and every
 * mismatch is a true bit error.  No printing happens inside the timed regions.
 */
static modem_result_t modem_run_chain(prbs_poly_t poly, uint16_t seed,
                                      float snr_db, uint32_t nbits) {
    prbs_t       tx;
    prbs_check_t chk;
    awgn_prng_t  rng;

    prbs_init(&tx, poly, seed);
    prbs_check_init(&chk, poly, seed);
    awgn_prng_seed(&rng, seed);

    /* Enable and zero the DWT cycle counter (same pattern as spi_perf.c). */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    uint32_t mod_cycles = 0, channel_cycles = 0, demod_cycles = 0;

    uint32_t remaining = nbits;
    while (remaining > 0u) {
        uint32_t n = (remaining < MODEM_BLOCK) ? remaining : MODEM_BLOCK;

        /* Stage 1 — modulation: bits -> BPSK symbols. */
        uint32_t t0 = dwt_now();
        for (uint32_t i = 0; i < n; i++) {
            g_sym_block[i] = bpsk_map(prbs_next_bit(&tx));
        }
        uint32_t t1 = dwt_now();

        /* Stage 2 — channel: add AWGN over the whole block. */
        channel_awgn_apply(g_sym_block, n, snr_db, &rng);
        uint32_t t2 = dwt_now();

        /* Stage 3 — demodulation: slice + BER check. */
        for (uint32_t i = 0; i < n; i++) {
            prbs_check_bit(&chk, bpsk_slice(g_sym_block[i]));
        }
        uint32_t t3 = dwt_now();

        mod_cycles     += t1 - t0;
        channel_cycles += t2 - t1;
        demod_cycles   += t3 - t2;
        remaining      -= n;
    }

    modem_result_t r;
    r.bits           = chk.total;
    r.errors         = chk.errors;
    r.theory         = channel_awgn_theory_ber(snr_db);
    r.mod_cycles     = mod_cycles;
    r.channel_cycles = channel_cycles;
    r.demod_cycles   = demod_cycles;
    return r;
}

/* ------------------------------------------------------------------ */
/* CLI command                                                        */
/* ------------------------------------------------------------------ */

static void print_run_usage(void) {
    printf("Usage:\n");
    printf("  modem run [--mod bpsk] [--snr <dB>] [--bits <N>]\n");
    printf("  modem sweep --snr <lo>:<hi>:<step> [--bits <N>]\n");
}

/* Confirm an optional "--mod" value is bpsk (the only modulation in B0). */
static int mod_is_ok(const char* args) {
    const char* m = find_flag(args, "--mod");
    if (m == NULL) {
        return 1; /* default: bpsk */
    }
    return (m[0] == 'b' && m[1] == 'p' && m[2] == 's' && m[3] == 'k' &&
            (m[4] == '\0' || m[4] == ' '));
}

static int cmd_modem_run(const char* args) {
    if (!mod_is_ok(args)) {
        printf("Only --mod bpsk is supported.\n");
        return 1;
    }

    float    snr_db = MODEM_DEFAULT_SNR_DB;
    uint32_t nbits  = MODEM_DEFAULT_RUN_BITS;

    const char* v = find_flag(args, "--snr");
    if (v != NULL && parse_float(v, &snr_db) == NULL) {
        printf("Invalid --snr value.\n");
        return 1;
    }
    v = find_flag(args, "--bits");
    if (v != NULL && parse_uint(v, &nbits) == NULL) {
        printf("Invalid --bits value.\n");
        return 1;
    }
    if (nbits == 0u) {
        printf("--bits must be > 0.\n");
        return 1;
    }

    modem_result_t r = modem_run_chain(MODEM_POLY, MODEM_SEED, snr_db, nbits);

    uint32_t total_cycles = r.mod_cycles + r.channel_cycles + r.demod_cycles;
    double   ber     = (r.bits > 0u) ? (double)r.errors / (double)r.bits : 0.0;
    double   nbf     = (r.bits > 0u) ? (double)r.bits : 1.0;

    printf("Eb/N0=%.2f dB  bits=%lu  errors=%lu\n",
           (double)snr_db, (unsigned long)r.bits, (unsigned long)r.errors);
    printf("  BER=%.3e  theory=%.3e\n", ber, r.theory);
    printf("  total : cycles=%lu  Mcycles=%.3f  cyc/bit=%.1f\n",
           (unsigned long)total_cycles, (double)total_cycles / 1.0e6,
           (double)total_cycles / nbf);
    printf("  mod   : cycles=%lu  cyc/bit=%.1f\n",
           (unsigned long)r.mod_cycles, (double)r.mod_cycles / nbf);
    printf("  chan  : cycles=%lu  cyc/bit=%.1f\n",
           (unsigned long)r.channel_cycles, (double)r.channel_cycles / nbf);
    printf("  demod : cycles=%lu  cyc/bit=%.1f\n",
           (unsigned long)r.demod_cycles, (double)r.demod_cycles / nbf);
    return 0;
}

static int cmd_modem_sweep(const char* args) {
    const char* v = find_flag(args, "--snr");
    if (v == NULL) {
        printf("sweep requires --snr <lo>:<hi>:<step>\n");
        return 1;
    }

    float lo = 0.0f, hi = 0.0f, step = 0.0f;
    const char* p = parse_float(v, &lo);
    if (p == NULL || *p != ':') {
        printf("Invalid sweep range; expected lo:hi:step\n");
        return 1;
    }
    p = parse_float(p + 1, &hi);
    if (p == NULL || *p != ':') {
        printf("Invalid sweep range; expected lo:hi:step\n");
        return 1;
    }
    p = parse_float(p + 1, &step);
    if (p == NULL || step <= 0.0f || hi < lo) {
        printf("Invalid sweep range; need step>0 and hi>=lo\n");
        return 1;
    }

    uint32_t nbits = MODEM_DEFAULT_SWEEP_BITS;
    v = find_flag(args, "--bits");
    if (v != NULL && (parse_uint(v, &nbits) == NULL || nbits == 0u)) {
        printf("Invalid --bits value.\n");
        return 1;
    }

    printf("Eb/N0(dB) |  errors |       BER  |    theory  | cyc/bit: mod /chan /demod\n");
    printf("----------+---------+------------+------------+--------------------------\n");
    printf_dma_flush();

    /* Add a small epsilon so the inclusive endpoint isn't lost to rounding. */
    for (float snr = lo; snr <= hi + step * 0.001f; snr += step) {
        modem_result_t r = modem_run_chain(MODEM_POLY, MODEM_SEED, snr, nbits);
        double nbf     = (r.bits > 0u) ? (double)r.bits : 1.0;
        double ber     = (r.bits > 0u) ? (double)r.errors / (double)r.bits : 0.0;
        printf("  %6.2f  | %7lu | %.3e | %.3e | %6.1f /%6.1f /%6.1f\n",
               (double)snr, (unsigned long)r.errors, ber, r.theory,
               (double)r.mod_cycles / nbf, (double)r.channel_cycles / nbf,
               (double)r.demod_cycles / nbf);
        printf_dma_flush();
    }
    return 0;
}

/* "modem ..." top-level command: dispatch on the first sub-token. */
static int cmd_modem(const char* args) {
    args = skip_ws(args);
    if (args[0] == 'r' && args[1] == 'u' && args[2] == 'n' &&
        (args[3] == '\0' || args[3] == ' ')) {
        return cmd_modem_run(skip_ws(args + 3));
    }
    if (args[0] == 's' && args[1] == 'w' && args[2] == 'e' && args[3] == 'e' &&
        args[4] == 'p' && (args[5] == '\0' || args[5] == ' ')) {
        return cmd_modem_sweep(skip_ws(args + 5));
    }
    print_run_usage();
    return 1;
}

static const cli_command_t commands[] = {
    {"modem", "BPSK modem sim: run|sweep (see 'modem')", cmd_modem},
};

/* ------------------------------------------------------------------ */
/* App boilerplate (mirrors apps/cli/cli_simple.c)                    */
/* ------------------------------------------------------------------ */

static void on_char_received(char ch) {
    cli_process_char(&g_cli, ch, uart_write);
    if (ch == '\n' || ch == '\r') {
        command_pending = 1;
    }
}

static void process_pending_command(void) {
    if (command_pending) {
        cli_history_save(&g_cli);
        cli_execute_command(&g_cli);
        g_cli.buffer_pos = 0;
        printf("\n> ");
        printf_dma_mark_pending();
        command_pending = 0;
    }
}

int main(void) {
    led2_init();
    uart_init();
    systick_init();
    sleep_mode_init();
    fault_handler_init();
    printf_dma_init();

    /*
     * Phase 1.9 handshake: tell the bootloader we booted past init so a clean
     * boot doesn't accumulate toward the fail-count cap. Resolve the slot from
     * the vector table base the bootloader programmed.
     */
    flash_slot_id_t boot_slot = ((SCB->VTOR & ~0x1FFu) >= FLASH_SLOT_B_BASE)
                                  ? FLASH_SLOT_B : FLASH_SLOT_A;
    (void)bl_handshake_clear_fail_count(boot_slot);

    uart_register_rx_callback(on_char_received);
    uart_register_tx_complete_callback(printf_dma_tx_complete_callback);

    size_t num_commands = sizeof(commands) / sizeof(commands[0]);
    cli_init(&g_cli, commands, num_commands, g_cmd_buffer, MODEM_CMD_SIZE);

    cli_print_welcome("\n=== STM32 Software BPSK Modem Simulator ===");
    printf("Try: modem run --snr 6 --bits 100000\n");
    printf("\n> ");
    printf_dma_mark_pending();

    while (1) {
        if (command_pending) {
            process_pending_command();
        }
        printf_dma_process();
        if (!command_pending) {
            enter_sleep_mode();
        }
    }
}
