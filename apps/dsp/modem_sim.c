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

/* Software modem core (Plan 002 B0.1/B0.2; RRC pulse shaping B0.4). */
#include "awgn.h"
#include "bpsk.h"
#include "fixed.h"
#include "prbs.h"
#include "rrc.h"

/*
 * Synchronisation & impairments (Plan 002 B0.5, issue #197). The complex
 * baseband primitives + impaired channel (PR 1) and the RX recovery loops
 * (PR 2) are wired into the demo here behind the opt-in --sync flag (PR 3),
 * so the default unshaped/shaped paths and their calibrated baselines stay
 * untouched.
 */
#include "complexq15.h"
#include "sincos.h"
#include "nco.h"
#include "impair.h"
#include "agc.h"
#include "timing_mm.h"
#include "costas.h"
#include "barker.h"

/* "modem run --snr 5.5 --bits 1000000 --shape" is ~42 chars; 64 leaves headroom. */
#define MODEM_CMD_SIZE 64

/* Defaults chosen so a bare `modem run` reproduces the issue's example. */
#define MODEM_DEFAULT_SNR_DB   6.0f
#define MODEM_DEFAULT_RUN_BITS 100000u
#define MODEM_DEFAULT_SWEEP_BITS 50000u
#define MODEM_SEED             1u
#define MODEM_POLY             PRBS9

/*
 * RRC pulse-shaping config (Plan 002 B0.4b, issue #207), matching the lib/dsp
 * defaults. The shaped chain is opt-in via the `--shape` flag so the default
 * one-sample-per-symbol path — and its calibrated HIL baselines — stay
 * unchanged.
 */
#define MODEM_SHAPE_BETA  0.35f
#define MODEM_SHAPE_SPS   4u
#define MODEM_SHAPE_SPAN  8u

/*
 * Sync chain config (Plan 002 B0.5, issue #197). The --sync path always uses
 * RRC shaping (recovery loops operate on real waveforms), prepends a Barker-13
 * preamble for frame sync, and passes the waveform through the impairment
 * channel (fractional timing + CFO + phase) before AWGN. The impairment
 * magnitudes below are the defaults a bare --sync applies; --timing/--cfo/--phase
 * override each independently so their individual BER/lock cost can be observed.
 *
 *   timing : fractional-sample delay in [0,1)         (default 0.4 sample)
 *   cfo    : carrier-frequency offset in cycles/sample (default 2e-4)
 *   phase  : static phase offset in radians            (default ~33 deg)
 *
 * Loop gains mirror the host e2e test (tests/lib/sync/test_recover_e2e.c): slow,
 * conservative gains that hold lock at 6 dB without cycle-slipping. The Barker
 * threshold is (0.55 * 13 * Esym)^2 with Esym ~ (agc_ref * 32768)^2 post-AGC.
 */
#define MODEM_SYNC_TIMING_MU   0.4f
#define MODEM_SYNC_CFO_CYCLES  2.0e-4f
#define MODEM_SYNC_PHASE_RAD   0.5759587f   /* ~33 deg (0x18000000 / 2^32 * 2pi) */

#define MODEM_SYNC_AGC_REF     0.7f
#define MODEM_SYNC_AGC_MU      0.005f
#define MODEM_SYNC_AGC_GAIN0   1.0f
#define MODEM_SYNC_MM_GAIN     0.004f
#define MODEM_SYNC_COSTAS_A    0.02f
#define MODEM_SYNC_COSTAS_B    0.0005f
#define MODEM_SYNC_BARKER_FRAC 0.55

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
    uint8_t  shaped;          /* 1 if RRC pulse shaping was applied       */
    uint8_t  synced;          /* 1 if the impaired+recovery chain ran     */
    uint8_t  locked;          /* 1 if Barker frame sync locked (sync only)*/
    int32_t  lock_sym;        /* symbol index of frame lock (-1 if none)  */
    uint32_t gen_cycles;      /* PRBS bit-stream generation               */
    uint32_t mod_cycles;      /* bit -> symbol (BPSK map)                 */
    uint32_t shape_cycles;    /* symbols -> oversampled waveform (TX RRC) */
    uint32_t channel_cycles;  /* samples -> noisy samples (AWGN[+impair]) */
    uint32_t match_cycles;    /* matched filter (RX RRC)                  */
    uint32_t demod_cycles;    /* sample at symbol instant -> rx bit       */
                              /* (sync: AGC+timing+Costas+Barker+slice)   */
    uint32_t check_cycles;    /* rx bit vs tx bit -> error count          */
} modem_result_t;

/*
 * Impairment magnitudes for the --sync chain. Each field is "off" at its
 * identity value, so a config zeroed except for --sync runs a clean (but
 * Barker-framed, shaped, complex-baseband) chain through the recovery loops.
 */
typedef struct {
    q15_t    timing_mu;   /* fractional-sample delay as q15 (0 = none)        */
    uint32_t cfo_incr;    /* per-sample phase increment (0 = no CFO)          */
    uint32_t phase0;      /* static phase offset, full circle 2^32 (0 = none) */
} modem_impair_t;

/*
 * Storing every symbol for a 100k-bit run would need ~200 KB (> 128 KB SRAM),
 * so the chain runs block-by-block.  Each block walks five separately-timed
 * stages — gen (PRBS), mod (BPSK map), channel (AWGN), demod (slice), check
 * (compare rx vs tx) — and the per-stage cycle deltas are accumulated across
 * blocks, isolating each component instead of one end-to-end number.  Block
 * processing also matches how channel_awgn_apply() is meant to be used (one
 * sigma/powf per block, not per bit).
 *
 * Errors are counted by comparing the sliced rx bits against the generated tx
 * bits directly (g_tx_block), so the check stage measures a true comparison
 * cost rather than a hidden second PRBS regeneration.
 */
#define MODEM_BLOCK 1024u

static uint8_t g_tx_block[MODEM_BLOCK];   /* generated tx bits (0/1)      */
static q15_t   g_sym_block[MODEM_BLOCK];  /* BPSK symbols (then noisy)    */
static uint8_t g_rx_block[MODEM_BLOCK];   /* sliced rx bits (0/1)         */

/*
 * Shaped-path scratch (only touched when --shape is given). The TX shaper turns
 * each block of up to MODEM_BLOCK symbols into MODEM_BLOCK*SPS oversampled
 * samples; the matched filter runs in place on that buffer. At SPS=4 this is
 * 8 KB of .bss — acceptable on the 128 KB part and shared across all runs.
 */
static q15_t g_samp_block[MODEM_BLOCK * MODEM_SHAPE_SPS];
static rrc_t g_tx_rrc;   /* TX pulse-shaping filter   */
static rrc_t g_rx_rrc;   /* RX matched filter         */

/*
 * Sync-chain scratch (only touched on the --sync path). The impaired+recovery
 * chain runs symbol-by-symbol, so payload bits and recovered decisions are
 * buffered to (a) prepend a Barker-13 preamble and (b) search the small
 * residual symbol delay between the recovered stream and the source after frame
 * lock (the matched-filter group delay; a real RX pins this with the preamble).
 * Capped at MODEM_SYNC_MAX_BITS so the two buffers stay a bounded ~32 KB of
 * .bss; --bits above the cap is clamped (and reported).
 */
#define MODEM_SYNC_MAX_BITS   16000u
#define MODEM_SYNC_DELAY_MAX  25       /* symbols searched for best alignment   */

static uint8_t g_sync_payload[MODEM_SYNC_MAX_BITS];
static uint8_t g_sync_rec[MODEM_SYNC_MAX_BITS + 64];

/* Read the DWT cycle counter (enabled once in modem_run_chain). */
static inline uint32_t dwt_now(void) {
    return DWT->CYCCNT;
}

/*
 * Run the unshaped PRBS -> BPSK -> AWGN -> slice -> compare chain for nbits,
 * timing the five stages separately.  One symbol is one sample (no pulse
 * shaping).  No printing happens inside the timed regions.  This is the default
 * path; its cycle/BER numbers feed the calibrated B0.3 HIL baselines, so it is
 * left byte-for-byte as it was.
 */
static modem_result_t modem_run_chain(prbs_poly_t poly, uint16_t seed,
                                      float snr_db, uint32_t nbits) {
    prbs_t      tx;
    awgn_prng_t rng;

    prbs_init(&tx, poly, seed);
    awgn_prng_seed(&rng, seed);

    /* Enable and zero the DWT cycle counter (same pattern as spi_perf.c). */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    uint32_t gen_cycles = 0, mod_cycles = 0, channel_cycles = 0,
             demod_cycles = 0, check_cycles = 0;
    uint64_t errors = 0;

    uint32_t remaining = nbits;
    while (remaining > 0u) {
        uint32_t n = (remaining < MODEM_BLOCK) ? remaining : MODEM_BLOCK;

        /* Stage 0 — gen: PRBS bit stream. */
        uint32_t t0 = dwt_now();
        prbs_next_bits(&tx, g_tx_block, n);

        /* Stage 1 — mod: bits -> BPSK symbols. */
        uint32_t t1 = dwt_now();
        bpsk_map_block(g_tx_block, g_sym_block, n);

        /* Stage 2 — channel: add AWGN over the whole block. */
        uint32_t t2 = dwt_now();
        channel_awgn_apply(g_sym_block, n, snr_db, &rng);

        /* Stage 3 — demod: slice noisy symbols -> rx bits. */
        uint32_t t3 = dwt_now();
        bpsk_slice_block(g_sym_block, g_rx_block, n);

        /* Stage 4 — check: compare rx bits against the tx bits. */
        uint32_t t4 = dwt_now();
        uint32_t block_errors = 0;
        for (uint32_t i = 0; i < n; i++) {
            if (g_rx_block[i] != g_tx_block[i]) {
                block_errors++;
            }
        }
        uint32_t t5 = dwt_now();

        gen_cycles     += t1 - t0;
        mod_cycles     += t2 - t1;
        channel_cycles += t3 - t2;
        demod_cycles   += t4 - t3;
        check_cycles   += t5 - t4;
        errors         += block_errors;
        remaining      -= n;
    }

    modem_result_t r;
    r.bits           = nbits;
    r.errors         = errors;
    r.theory         = channel_awgn_theory_ber(snr_db);
    r.shaped         = 0u;
    r.gen_cycles     = gen_cycles;
    r.mod_cycles     = mod_cycles;
    r.shape_cycles   = 0u;
    r.channel_cycles = channel_cycles;
    r.match_cycles   = 0u;
    r.demod_cycles   = demod_cycles;
    r.check_cycles   = check_cycles;
    return r;
}

/*
 * Run the shaped chain for nbits: PRBS -> BPSK -> RRC TX shape -> AWGN (at
 * sample rate) -> RRC matched filter -> decimate at symbol instants -> slice ->
 * compare.  Seven stages are timed separately (gen/mod/shape/channel/match/
 * demod/check).  The AWGN seam is unchanged — it just sees SPS x more samples.
 *
 * Symbol k peaks in the matched-filter output at absolute sample index
 * k*SPS + chain_delay, so we sample there.  A reference PRBS (seeded identically
 * to the transmitter) is advanced once per decimated symbol to count errors,
 * which keeps transmitter and checker aligned across the filter delay without a
 * symbol FIFO (the same technique tests/lib/dsp/test_rrc.c uses).  Each payload
 * symbol is followed through the filters by trailing zero symbols so the last
 * one flushes out and is decided.  No printing inside the timed regions.
 */
static modem_result_t modem_run_chain_shaped(prbs_poly_t poly, uint16_t seed,
                                             float snr_db, uint32_t nbits) {
    prbs_t       tx;
    prbs_check_t chk;
    awgn_prng_t  rng;

    prbs_init(&tx, poly, seed);
    prbs_check_init(&chk, poly, seed);
    awgn_prng_seed(&rng, seed);

    rrc_design(&g_tx_rrc, MODEM_SHAPE_BETA, MODEM_SHAPE_SPS, MODEM_SHAPE_SPAN);
    rrc_design(&g_rx_rrc, MODEM_SHAPE_BETA, MODEM_SHAPE_SPS, MODEM_SHAPE_SPAN);

    const uint32_t sps = MODEM_SHAPE_SPS;
    const size_t   delay_samples = rrc_chain_delay(&g_tx_rrc);

    /* Pad with one chain delay (rounded up to whole symbols) plus one symbol so
     * the final payload symbol flushes through both filters to the decimator. */
    size_t tail_syms   = delay_samples / sps + 1u;
    uint32_t total_syms = nbits + (uint32_t)tail_syms;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    uint32_t gen_cycles = 0, mod_cycles = 0, shape_cycles = 0, channel_cycles = 0,
             match_cycles = 0, demod_cycles = 0, check_cycles = 0;
    uint64_t errors = 0;

    size_t sample_base = 0;                 /* abs index of g_samp_block[0]      */
    size_t next_peak   = delay_samples;     /* abs sample index of next symbol   */
    uint32_t produced  = 0;                 /* decimated payload symbols so far  */

    uint32_t sym_done = 0;
    while (sym_done < total_syms) {
        uint32_t n = total_syms - sym_done;
        if (n > MODEM_BLOCK) {
            n = MODEM_BLOCK;
        }
        uint32_t payload_n = 0;
        if (sym_done < nbits) {
            payload_n = nbits - sym_done;
            if (payload_n > n) {
                payload_n = n;
            }
        }

        /* Stage 0 — gen: PRBS bits for the payload symbols in this block. */
        uint32_t t0 = dwt_now();
        if (payload_n > 0u) {
            prbs_next_bits(&tx, g_tx_block, payload_n);
        }

        /* Stage 1 — mod: payload bits -> symbols; tail symbols are zero. */
        uint32_t t1 = dwt_now();
        for (uint32_t i = 0; i < payload_n; i++) {
            g_sym_block[i] = bpsk_map(g_tx_block[i]);
        }
        for (uint32_t i = payload_n; i < n; i++) {
            g_sym_block[i] = 0;
        }

        /* Stage 2 — shape: n symbols -> n*SPS oversampled samples (TX RRC). */
        uint32_t t2 = dwt_now();
        rrc_tx_shape(&g_tx_rrc, g_sym_block, n, g_samp_block);

        /* Stage 3 — channel: AWGN over the whole oversampled block. */
        uint32_t t3 = dwt_now();
        channel_awgn_apply(g_samp_block, (size_t)n * sps, snr_db, &rng);

        /* Stage 4 — match: RX matched filter, in place. */
        uint32_t t4 = dwt_now();
        rrc_rx_match(&g_rx_rrc, g_samp_block, (size_t)n * sps, g_samp_block);

        /* Stage 5 — demod: slice the matched-filter output at symbol instants. */
        uint32_t t5 = dwt_now();
        uint32_t dec_n = 0;
        for (uint32_t p = 0; p < n * sps; p++) {
            size_t abs = sample_base + p;
            if (abs == next_peak && (produced + dec_n) < nbits) {
                g_rx_block[dec_n++] = bpsk_slice(g_samp_block[p]);
                next_peak += sps;
            }
        }

        /* Stage 6 — check: compare decimated rx bits against the reference. */
        uint32_t t6 = dwt_now();
        for (uint32_t i = 0; i < dec_n; i++) {
            if (!prbs_check_bit(&chk, g_rx_block[i])) {
                errors++;
            }
        }
        uint32_t t7 = dwt_now();

        gen_cycles     += t1 - t0;
        mod_cycles     += t2 - t1;
        shape_cycles   += t3 - t2;
        channel_cycles += t4 - t3;
        match_cycles   += t5 - t4;
        demod_cycles   += t6 - t5;
        check_cycles   += t7 - t6;

        produced    += dec_n;
        sample_base += (size_t)n * sps;
        sym_done    += n;
    }

    modem_result_t r;
    r.bits           = produced;   /* compared symbols (== nbits once flushed)  */
    r.errors         = errors;
    r.theory         = channel_awgn_theory_ber(snr_db);
    r.shaped         = 1u;
    r.gen_cycles     = gen_cycles;
    r.mod_cycles     = mod_cycles;
    r.shape_cycles   = shape_cycles;
    r.channel_cycles = channel_cycles;
    r.match_cycles   = match_cycles;
    r.demod_cycles   = demod_cycles;
    r.check_cycles   = check_cycles;
    return r;
}

/*
 * Run the impaired-channel + RX-recovery chain (Plan 002 B0.5).  A Barker-13
 * preamble is prepended to nbits of PRBS payload; each symbol is RRC-shaped,
 * passed through the impairment channel (fractional timing + CFO + phase) and
 * complex-baseband AWGN, then run through the full receiver:
 *
 *   matched filter -> AGC -> M&M timing recovery -> Costas phase recovery
 *   -> Barker frame sync / polarity -> slice -> compare
 *
 * This is the on-board twin of tests/lib/sync/test_recover_e2e.c.  The chain
 * runs one symbol at a time (the loops are inherently sequential), so the only
 * separately-timed stages reported are gen (PRBS), channel (shape+impair+AWGN,
 * lumped because they interleave per symbol), and demod (the whole RX recovery
 * pipeline).  Errors are counted after the run by aligning the recovered stream
 * to the source over the small matched-filter group delay, exactly as the host
 * e2e test does; before frame lock no payload bits are emitted.
 *
 * nbits is clamped to MODEM_SYNC_MAX_BITS (the caller is told via r.bits).
 */
static modem_result_t modem_run_chain_sync(prbs_poly_t poly, uint16_t seed,
                                           float snr_db, uint32_t nbits,
                                           const modem_impair_t *imp_cfg) {
    if (nbits > MODEM_SYNC_MAX_BITS) {
        nbits = MODEM_SYNC_MAX_BITS;
    }

    const uint32_t sps = MODEM_SHAPE_SPS;

    rrc_design(&g_tx_rrc, MODEM_SHAPE_BETA, sps, MODEM_SHAPE_SPAN);
    rrc_t rx_i, rx_q;                       /* matched filter per I/Q component */
    rrc_design(&rx_i, MODEM_SHAPE_BETA, sps, MODEM_SHAPE_SPAN);
    rrc_design(&rx_q, MODEM_SHAPE_BETA, sps, MODEM_SHAPE_SPAN);

    channel_impair_cfg_t cfg = { imp_cfg->cfo_incr, imp_cfg->phase0,
                                 imp_cfg->timing_mu };
    channel_impair_state_t imp;
    channel_impair_init(&imp, &cfg);

    awgn_prng_t rng;
    awgn_prng_seed(&rng, seed);

    agc_t agc;
    agc_init(&agc, MODEM_SYNC_AGC_REF, MODEM_SYNC_AGC_MU, MODEM_SYNC_AGC_GAIN0);
    timing_mm_t mm;
    timing_mm_init(&mm, (uint8_t)sps, MODEM_SYNC_MM_GAIN);
    costas_t cos;
    costas_init(&cos, MODEM_SYNC_COSTAS_A, MODEM_SYNC_COSTAS_B);
    barker_t bar;
    double  esym = MODEM_SYNC_AGC_REF * 32768.0;
    int64_t thr  = (int64_t)((MODEM_SYNC_BARKER_FRAC * 13.0 * esym) *
                             (MODEM_SYNC_BARKER_FRAC * 13.0 * esym));
    barker_init(&bar, thr);

    prbs_t txbits;
    prbs_init(&txbits, poly, seed);
    for (uint32_t i = 0; i < nbits; i++) {
        g_sync_payload[i] = prbs_next_bit(&txbits);
    }

    const uint32_t nsyms = BARKER13_LEN + nbits;
    int      locked = 0, polarity = 1;
    int32_t  lock_sym = -1;
    uint32_t nrec = 0;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    uint32_t gen_cycles = 0, channel_cycles = 0, demod_cycles = 0;

    q15_t  txsamp[MODEM_SHAPE_SPS];
    cq15_t imp_out[MODEM_SHAPE_SPS];

    for (uint32_t k = 0; k < nsyms; k++) {
        /* Stage 0 — gen/map: Barker preamble symbol, then a payload symbol. */
        uint32_t t0 = dwt_now();
        q15_t sym;
        if (k < (uint32_t)BARKER13_LEN) {
            sym = BARKER13[k] > 0 ? bpsk_map(1) : bpsk_map(0);
        } else {
            sym = bpsk_map(g_sync_payload[k - BARKER13_LEN]);
        }

        /* Stage 1 — channel: shape -> impair (timing/CFO/phase) -> AWGN. */
        uint32_t t1 = dwt_now();
        rrc_tx_shape(&g_tx_rrc, &sym, 1, txsamp);
        channel_impair_apply(&imp, &cfg, txsamp, imp_out, sps);
        channel_awgn_apply_cq15(imp_out, sps, snr_db, &rng);

        /* Stage 2 — demod: full RX recovery, sample-by-sample. */
        uint32_t t2 = dwt_now();
        for (uint32_t p = 0; p < sps; p++) {
            cq15_t mf = cq15_make(rrc_push(&rx_i, imp_out[p].re),
                                  rrc_push(&rx_q, imp_out[p].im));
            cq15_t g = agc_apply(&agc, mf);
            cq15_t sym_out;
            if (!timing_mm_push(&mm, g, &sym_out)) {
                continue;
            }
            cq15_t y = costas_step(&cos, sym_out);

            if (!locked) {
                int32_t corr_re;
                if (barker_push(&bar, y, &corr_re, NULL)) {
                    locked   = 1;
                    lock_sym = (int32_t)k;
                    polarity = (corr_re >= 0) ? 1 : -1;   /* resolve 180 deg */
                }
            } else if (nrec < (uint32_t)(sizeof(g_sync_rec))) {
                q15_t corrected = (polarity >= 0) ? y.re : (q15_t)(-y.re);
                g_sync_rec[nrec++] = bpsk_slice(corrected);
            }
        }
        uint32_t t3 = dwt_now();

        gen_cycles     += t1 - t0;
        channel_cycles += t2 - t1;
        demod_cycles   += t3 - t2;
    }

    /*
     * Align the recovered stream to the source over the matched-filter group
     * delay and count errors at the best alignment (check stage), mirroring the
     * host e2e test.  Without lock, nrec is 0 and BER folds to the no-lock floor.
     */
    uint32_t t4 = dwt_now();
    uint64_t best_err = nrec;
    uint64_t total    = (nrec > 0u) ? 1u : 0u;
    for (uint32_t D = 0; D < (uint32_t)MODEM_SYNC_DELAY_MAX && D < nrec; D++) {
        uint64_t err = 0, cnt = 0;
        for (uint32_t i = 0; i + D < nrec && i < nbits; i++) {
            if (g_sync_rec[i + D] != g_sync_payload[i]) {
                err++;
            }
            cnt++;
        }
        if (cnt > 0u && err < best_err) {
            best_err = err;
            total    = cnt;
        }
    }
    uint32_t check_cycles = dwt_now() - t4;

    modem_result_t r;
    r.bits           = total;
    r.errors         = best_err;
    r.theory         = channel_awgn_theory_ber(snr_db);
    r.shaped         = 1u;
    r.synced         = 1u;
    r.locked         = (uint8_t)locked;
    r.lock_sym       = lock_sym;
    r.gen_cycles     = gen_cycles;
    r.mod_cycles     = 0u;
    r.shape_cycles   = 0u;
    r.channel_cycles = channel_cycles;
    r.match_cycles   = 0u;
    r.demod_cycles   = demod_cycles;
    r.check_cycles   = check_cycles;
    return r;
}

/* ------------------------------------------------------------------ */
/* CLI command                                                        */
/* ------------------------------------------------------------------ */

static void print_run_usage(void) {
    printf("Usage:\n");
    printf("  modem run [--mod bpsk] [--snr <dB>] [--bits <N>] [--shape|--sync]\n");
    printf("  modem sweep --snr <lo>:<hi>:<step> [--bits <N>] [--shape|--sync]\n");
    printf("  --shape: RRC pulse shaping (b=0.35, sps=4, span=8) at sample rate\n");
    printf("  --sync : impaired channel + RX recovery (AGC/M&M/Costas/Barker)\n");
    printf("           default timing=0.4 cfo=2e-4 phase=33deg; override with\n");
    printf("           --timing <mu> --cfo <cyc/samp> --phase <rad> (--no-* to zero)\n");
    printf("           --bits clamped to %u on this path\n", MODEM_SYNC_MAX_BITS);
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

/* "--shape" is a valueless toggle: present -> shaped chain, absent -> default. */
static int shape_requested(const char* args) {
    return find_flag(args, "--shape") != NULL;
}

/* "--sync" is a valueless toggle selecting the impaired + recovery chain. */
static int sync_requested(const char* args) {
    return find_flag(args, "--sync") != NULL;
}

/*
 * Build the impairment config for the --sync chain. Each impairment defaults to
 * the calibrated magnitude (see MODEM_SYNC_* above) and is overridden by an
 * explicit --timing/--cfo/--phase value, or zeroed by --no-timing/--no-cfo/
 * --no-phase, so each impairment's individual BER/lock cost can be isolated.
 * Invalid numeric values leave the default in place (the run still proceeds).
 */
static modem_impair_t modem_build_impair(const char* args) {
    float timing_mu  = MODEM_SYNC_TIMING_MU;
    float cfo_cycles = MODEM_SYNC_CFO_CYCLES;
    float phase_rad  = MODEM_SYNC_PHASE_RAD;

    const char* v;
    if (find_flag(args, "--no-timing") != NULL) {
        timing_mu = 0.0f;
    } else if ((v = find_flag(args, "--timing")) != NULL) {
        (void)parse_float(v, &timing_mu);
    }
    if (find_flag(args, "--no-cfo") != NULL) {
        cfo_cycles = 0.0f;
    } else if ((v = find_flag(args, "--cfo")) != NULL) {
        (void)parse_float(v, &cfo_cycles);
    }
    if (find_flag(args, "--no-phase") != NULL) {
        phase_rad = 0.0f;
    } else if ((v = find_flag(args, "--phase")) != NULL) {
        (void)parse_float(v, &phase_rad);
    }

    modem_impair_t c;
    /* Clamp the fractional delay into [0,1) before quantising to q15. */
    if (timing_mu < 0.0f) timing_mu = 0.0f;
    if (timing_mu > 0.999f) timing_mu = 0.999f;
    c.timing_mu = (q15_t)(timing_mu * 32768.0f);
    c.cfo_incr  = nco_phase_from_cycles(cfo_cycles);
    c.phase0    = nco_phase_from_rad(phase_rad);
    return c;
}

/* Dispatch to the unshaped, shaped, or impaired+recovery chain. */
static modem_result_t modem_run_dispatch(float snr_db, uint32_t nbits,
                                         int shaped, int synced,
                                         const modem_impair_t* imp) {
    if (synced) {
        return modem_run_chain_sync(MODEM_POLY, MODEM_SEED, snr_db, nbits, imp);
    }
    if (shaped) {
        return modem_run_chain_shaped(MODEM_POLY, MODEM_SEED, snr_db, nbits);
    }
    return modem_run_chain(MODEM_POLY, MODEM_SEED, snr_db, nbits);
}

/* Sum of all timed stages (shaped stages are zero on the unshaped path). */
static uint32_t modem_total_cycles(const modem_result_t* r) {
    return r->gen_cycles + r->mod_cycles + r->shape_cycles + r->channel_cycles +
           r->match_cycles + r->demod_cycles + r->check_cycles;
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

    int shaped = shape_requested(args);
    int synced = sync_requested(args);
    if (shaped && synced) {
        printf("--shape and --sync are mutually exclusive (--sync already shapes).\n");
        return 1;
    }
    modem_impair_t imp = modem_build_impair(args);
    modem_result_t r = modem_run_dispatch(snr_db, nbits, shaped, synced, &imp);

    uint32_t total_cycles = modem_total_cycles(&r);
    double   ber = (r.bits > 0u) ? (double)r.errors / (double)r.bits : 0.0;
    double   nbf = (r.bits > 0u) ? (double)r.bits : 1.0;

    const char* mode = synced ? "sync" : (shaped ? "rrc" : "off");
    printf("Eb/N0=%.2f dB  bits=%lu  errors=%lu  shaping=%s\n",
           (double)snr_db, (unsigned long)r.bits, (unsigned long)r.errors, mode);
    if (synced) {
        printf("  lock=%s", r.locked ? "yes" : "NO");
        if (r.locked) {
            printf(" @sym=%ld", (long)r.lock_sym);
        }
        printf("  (degradation vs ideal-sync theory bounded below)\n");
    }
    printf("  BER=%.3e  theory=%.3e\n", ber, r.theory);
    printf("  total : cycles=%lu  Mcycles=%.3f  cyc/bit=%.1f\n",
           (unsigned long)total_cycles, (double)total_cycles / 1.0e6,
           (double)total_cycles / nbf);
    printf("  gen   : cycles=%lu  cyc/bit=%.1f\n",
           (unsigned long)r.gen_cycles, (double)r.gen_cycles / nbf);
    if (!synced) {
        printf("  mod   : cycles=%lu  cyc/bit=%.1f\n",
               (unsigned long)r.mod_cycles, (double)r.mod_cycles / nbf);
    }
    if (shaped) {
        printf("  shape : cycles=%lu  cyc/bit=%.1f\n",
               (unsigned long)r.shape_cycles, (double)r.shape_cycles / nbf);
    }
    printf("  chan  : cycles=%lu  cyc/bit=%.1f%s\n",
           (unsigned long)r.channel_cycles, (double)r.channel_cycles / nbf,
           synced ? "  (shape+impair+AWGN)" : "");
    if (shaped) {
        printf("  match : cycles=%lu  cyc/bit=%.1f\n",
               (unsigned long)r.match_cycles, (double)r.match_cycles / nbf);
    }
    printf("  demod : cycles=%lu  cyc/bit=%.1f%s\n",
           (unsigned long)r.demod_cycles, (double)r.demod_cycles / nbf,
           synced ? "  (MF+AGC+M&M+Costas+Barker+slice)" : "");
    printf("  check : cycles=%lu  cyc/bit=%.1f\n",
           (unsigned long)r.check_cycles, (double)r.check_cycles / nbf);
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

    int shaped = shape_requested(args);
    int synced = sync_requested(args);
    if (shaped && synced) {
        printf("--shape and --sync are mutually exclusive (--sync already shapes).\n");
        return 1;
    }
    modem_impair_t imp = modem_build_impair(args);

    const char* mode = synced ? "sync" : (shaped ? "rrc" : "off");
    if (synced) {
        printf("Eb/N0(dB) | lock |  errors |       BER  |    theory  | tot cyc/bit  (shaping=%s)\n",
               mode);
        printf("----------+------+---------+------------+------------+------------\n");
    } else {
        printf("Eb/N0(dB) |  errors |       BER  |    theory  | tot cyc/bit  (shaping=%s)\n",
               mode);
        printf("----------+---------+------------+------------+------------\n");
    }
    printf_dma_flush();

    /* Add a small epsilon so the inclusive endpoint isn't lost to rounding. */
    for (float snr = lo; snr <= hi + step * 0.001f; snr += step) {
        modem_result_t r = modem_run_dispatch(snr, nbits, shaped, synced, &imp);
        double nbf = (r.bits > 0u) ? (double)r.bits : 1.0;
        double ber = (r.bits > 0u) ? (double)r.errors / (double)r.bits : 0.0;
        uint32_t total = modem_total_cycles(&r);
        if (synced) {
            printf("  %6.2f  | %4s | %7lu | %.3e | %.3e | %10.1f\n",
                   (double)snr, r.locked ? "yes" : "NO",
                   (unsigned long)r.errors, ber, r.theory, (double)total / nbf);
        } else {
            printf("  %6.2f  | %7lu | %.3e | %.3e | %10.1f\n",
                   (double)snr, (unsigned long)r.errors, ber, r.theory,
                   (double)total / nbf);
        }
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
