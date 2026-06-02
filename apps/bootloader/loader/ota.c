#include "ota.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "stm32f4xx.h"

#include "flash.h"
#include "flash_slot.h"
#include "framing.h"
#include "img_header.h"
#include "led2.h"
#include "rtc_backup.h"
#include "uart.h"
#include "verify.h"

/*
 * Plan 001 Phase 1.8 — bootloader OTA receiver.
 *
 * Wire protocol (over USART2 at 115200, framed via lib/framing):
 *
 *   Host ──> Device  PING                 (no payload)
 *   Device ─> Host   PONG                 (no payload)
 *   Host ──> Device  OTA_BEGIN            payload = u8 slot, u32 total_size
 *   Device ─> Host   ACK / NACK
 *   Host ──> Device  OTA_CHUNK            payload = u32 offset, then data
 *   Device ─> Host   ACK / NACK
 *     (chunk loop continues until OTA_END)
 *   Host ──> Device  OTA_END              (no payload)
 *   Device ─> Host   STATUS               payload = u8 ota_status_t
 *   Device performs NVIC_SystemReset()    (on STATUS=ok), or halts.
 *
 * Constraints
 *   - The previously-active slot is NEVER touched. OTA_BEGIN refuses to
 *     write to the active slot.
 *   - The freshly-written slot is verified with the SAME path normal boot
 *     uses (header parse → SHA-256 → ECDSA). On verify failure the metadata
 *     is left untouched, so the previously-active slot remains active.
 *   - On verify success a new img_slot_metadata_t is committed for the
 *     target slot with active=1, fail_count=0, monotonic_counter advanced
 *     past both slots. The previously-active slot's metadata is then
 *     re-committed with active=0 — that final write is what makes the swap
 *     visible to the next boot.
 *   - A power-cut between the two metadata commits leaves both slots
 *     active. The decision tree in main.c::pick_active_slot() resolves that
 *     by picking the higher monotonic_counter, which is by construction the
 *     freshly-OTA'd slot.
 */

#define OTA_CHUNK_HEADER_BYTES   4u                              /* offset */
#define OTA_CHUNK_MAX_DATA       (FRAME_MAX_PAYLOAD - OTA_CHUNK_HEADER_BYTES)

/* Framing decoder needs a payload buffer; sized for the largest frame. */
static uint8_t s_decoder_payload[FRAME_MAX_PAYLOAD];

typedef enum {
    OTA_STATE_AWAIT_BEGIN = 0,
    OTA_STATE_RECEIVING   = 1,
    OTA_STATE_DONE        = 2,  /* OTA_END seen, awaiting verify result */
    OTA_STATE_HALT        = 3,
} ota_state_t;

typedef struct {
    ota_state_t      state;
    flash_slot_id_t  target_slot;
    flash_slot_id_t  prev_active;     /* slot to deactivate on success */
    uint32_t         expected_size;   /* declared in OTA_BEGIN */
    uint32_t         received_bytes;  /* sum of OTA_CHUNK data we wrote */
    uint32_t         next_seq;        /* expected SEQ for the next chunk */
    uint8_t          last_rx_seq;     /* most recent SEQ we ACK'd */
    uint8_t          have_last_rx_seq;
    uint32_t         max_seen_counter;/* metadata monotonic floor */
} ota_session_t;

static ota_session_t s_sess;

/* ------------------------------------------------------------------------
 * Wire helpers
 * ------------------------------------------------------------------------ */

static void uart_write_bytes(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        uart_write((char)buf[i]);
    }
}

static void send_frame(uint8_t seq, frame_type_t type,
                       const uint8_t *payload, size_t len)
{
    /* Encoded buffer is small enough to live on the stack — sector-0
     * bootloader has no spare BSS and a 1-frame buffer is the only
     * outbound state we need. */
    static uint8_t out_buf[FRAME_MAX_ENCODED_SIZE];
    int n = frame_encode(seq, type, payload, len, out_buf, sizeof(out_buf));
    if (n < 0) {
        return;
    }
    uart_write_bytes(out_buf, (size_t)n);
}

static void send_ack(uint8_t seq)   { send_frame(seq, FRAME_TYPE_ACK,  NULL, 0); }
static void send_nack(uint8_t seq)  { send_frame(seq, FRAME_TYPE_NACK, NULL, 0); }
static void send_pong(uint8_t seq)  { send_frame(seq, FRAME_TYPE_PONG, NULL, 0); }

static void send_status(uint8_t seq, ota_status_t status)
{
    uint8_t b = (uint8_t)status;
    send_frame(seq, FRAME_TYPE_STATUS, &b, 1);
}

/* ------------------------------------------------------------------------
 * Metadata bookkeeping
 * ------------------------------------------------------------------------ */

static int read_metadata(flash_slot_id_t slot, img_slot_metadata_t *out)
{
    const uint32_t addr = flash_slot_metadata_address(slot);
    img_err_t rc = img_slot_metadata_parse((const uint8_t *)addr,
                                           sizeof(img_slot_metadata_t),
                                           out);
    return (rc == IMG_OK) ? 1 : 0;
}

/*
 * Collect the highest monotonic_counter across both slots so the
 * freshly-flashed slot's new metadata can advance past both. Falls back
 * to 0 if neither slot has valid metadata (fresh chip case).
 */
static uint32_t scan_max_monotonic(void)
{
    uint32_t hi = 0;
    img_slot_metadata_t md;
    if (read_metadata(FLASH_SLOT_A, &md) && md.monotonic_counter > hi) {
        hi = md.monotonic_counter;
    }
    if (read_metadata(FLASH_SLOT_B, &md) && md.monotonic_counter > hi) {
        hi = md.monotonic_counter;
    }
    return hi;
}

/*
 * Determine which slot is currently active. If neither slot has
 * valid metadata we treat slot A as active so the only target left
 * for OTA is slot B — a fresh chip's first OTA always lands in B.
 */
static flash_slot_id_t scan_active_slot(void)
{
    img_slot_metadata_t a, b;
    int a_ok = read_metadata(FLASH_SLOT_A, &a);
    int b_ok = read_metadata(FLASH_SLOT_B, &b);

    if (a_ok && b_ok) {
        if (a.active && !b.active) return FLASH_SLOT_A;
        if (b.active && !a.active) return FLASH_SLOT_B;
        return (b.monotonic_counter > a.monotonic_counter) ? FLASH_SLOT_B
                                                            : FLASH_SLOT_A;
    }
    if (a_ok && a.active) return FLASH_SLOT_A;
    if (b_ok && b.active) return FLASH_SLOT_B;
    return FLASH_SLOT_A;
}

static void compute_metadata_crc(img_slot_metadata_t *md)
{
    md->magic            = IMG_SLOT_METADATA_MAGIC;
    md->metadata_version = IMG_SLOT_METADATA_VERSION;
    md->reserved[0] = md->reserved[1] = md->reserved[2] = 0u;
    const size_t crc_offset = sizeof(*md) - sizeof(uint32_t);
    md->metadata_crc = img_crc32((const uint8_t *)md, crc_offset);
}

/*
 * Atomically swap the active slot.
 *
 *   1. Build target metadata with active=1, monotonic_counter = max+1.
 *   2. flash_slot_commit_metadata(target) — erase + program + readback.
 *   3. Build previously-active metadata with active=0, monotonic_counter
 *      preserved (so anti-rollback in Phase 1.9 still has the old floor).
 *   4. flash_slot_commit_metadata(prev_active).
 *
 * A power cut between (2) and (4) leaves both slots' active bits set; the
 * existing slot-pick decision tree falls back to the higher
 * monotonic_counter, which is by construction the new slot — so the next
 * boot still picks the freshly-OTA'd image. (4) merely tidies up.
 */
static int swap_active_slot(flash_slot_id_t target, flash_slot_id_t previous,
                            uint32_t new_counter)
{
    img_slot_metadata_t md_target;
    memset(&md_target, 0, sizeof(md_target));
    md_target.active            = 1u;
    md_target.fail_count        = 0u;
    md_target.monotonic_counter = new_counter;
    compute_metadata_crc(&md_target);

    if (flash_slot_commit_metadata(target, &md_target) != ERR_OK) {
        return 0;
    }

    img_slot_metadata_t md_prev;
    memset(&md_prev, 0, sizeof(md_prev));
    md_prev.active            = 0u;
    md_prev.fail_count        = 0u;
    /* Preserve previous slot's counter floor when known; otherwise zero is
     * fine because the new slot's counter (= max+1) already dominates. */
    img_slot_metadata_t old;
    if (read_metadata(previous, &old)) {
        md_prev.monotonic_counter = old.monotonic_counter;
    }
    compute_metadata_crc(&md_prev);

    if (flash_slot_commit_metadata(previous, &md_prev) != ERR_OK) {
        /* Target metadata is already committed — next boot still picks
         * target (highest counter). Tolerable; report success. */
        return 1;
    }

    return 1;
}

/* ------------------------------------------------------------------------
 * Frame handlers
 * ------------------------------------------------------------------------ */

static int handle_ota_begin(const uint8_t *p, size_t n, uint8_t seq)
{
    if (n < 1u + 4u) {
        send_nack(seq);
        return 0;
    }
    uint8_t  slot_id      = p[0];
    uint32_t total_size   = (uint32_t)p[1]
                          | ((uint32_t)p[2] << 8)
                          | ((uint32_t)p[3] << 16)
                          | ((uint32_t)p[4] << 24);

    if (slot_id != (uint8_t)FLASH_SLOT_A && slot_id != (uint8_t)FLASH_SLOT_B) {
        send_nack(seq);
        return 0;
    }
    flash_slot_id_t target = (flash_slot_id_t)slot_id;
    if (target == s_sess.prev_active) {
        /* Refuse to overwrite the currently-active slot. */
        uart_puts("OTA: refusing to write active slot\r\n");
        send_nack(seq);
        return 0;
    }
    if (total_size == 0u || total_size > FLASH_SLOT_SIZE) {
        send_nack(seq);
        return 0;
    }

    uart_puts("OTA: BEGIN slot=");
    uart_puts((target == FLASH_SLOT_A) ? "A" : "B");
    uart_puts(" size=");
    uart_print_dec32(total_size);
    uart_puts("\r\n");

    if (flash_slot_erase(target) != ERR_OK) {
        send_nack(seq);
        return 0;
    }

    s_sess.target_slot    = target;
    s_sess.expected_size  = total_size;
    s_sess.received_bytes = 0u;
    s_sess.state          = OTA_STATE_RECEIVING;
    send_ack(seq);
    return 1;
}

static int handle_ota_chunk(const uint8_t *p, size_t n, uint8_t seq)
{
    if (s_sess.state != OTA_STATE_RECEIVING) {
        send_nack(seq);
        return 0;
    }
    if (n < OTA_CHUNK_HEADER_BYTES) {
        send_nack(seq);
        return 0;
    }
    uint32_t offset = (uint32_t)p[0]
                    | ((uint32_t)p[1] << 8)
                    | ((uint32_t)p[2] << 16)
                    | ((uint32_t)p[3] << 24);
    size_t   data_len = n - OTA_CHUNK_HEADER_BYTES;
    const uint8_t *data = p + OTA_CHUNK_HEADER_BYTES;

    if ((uint64_t)offset + (uint64_t)data_len > (uint64_t)s_sess.expected_size) {
        send_nack(seq);
        return 0;
    }
    uint32_t addr = flash_slot_base_address(s_sess.target_slot) + offset;
    if (flash_slot_validate_range(addr, data_len) != ERR_OK) {
        send_nack(seq);
        return 0;
    }

    err_t rc = flash_unlock();
    if (rc != ERR_OK) {
        send_nack(seq);
        return 0;
    }
    rc = flash_write_bytes(addr, data, data_len);
    flash_lock();
    if (rc != ERR_OK) {
        send_nack(seq);
        return 0;
    }
    s_sess.received_bytes += (uint32_t)data_len;
    send_ack(seq);
    return 1;
}

static void handle_ota_end(uint8_t seq)
{
    if (s_sess.state != OTA_STATE_RECEIVING) {
        send_status(seq, OTA_STATUS_PROTOCOL_ERROR);
        s_sess.state = OTA_STATE_HALT;
        return;
    }
    if (s_sess.received_bytes != s_sess.expected_size) {
        uart_puts("OTA: END but size mismatch\r\n");
        send_status(seq, OTA_STATUS_WRITE_FAILED);
        s_sess.state = OTA_STATE_HALT;
        return;
    }

    uart_puts("OTA: END verifying...\r\n");

    uint32_t app_base = 0u;
    uint32_t cycles   = 0u;
    verify_status_t vs = verify_slot(s_sess.target_slot, &app_base, &cycles);
    if (vs != VERIFY_OK) {
        uart_puts("OTA: verify FAILED\r\n");
        send_status(seq, OTA_STATUS_VERIFY_FAILED);
        s_sess.state = OTA_STATE_HALT;
        return;
    }

    uint32_t new_counter = s_sess.max_seen_counter + 1u;
    if (!swap_active_slot(s_sess.target_slot, s_sess.prev_active, new_counter)) {
        uart_puts("OTA: metadata commit failed\r\n");
        send_status(seq, OTA_STATUS_WRITE_FAILED);
        s_sess.state = OTA_STATE_HALT;
        return;
    }

    uart_puts("OTA: ok slot=");
    uart_puts((s_sess.target_slot == FLASH_SLOT_A) ? "A" : "B");
    uart_puts("\r\n");
    send_status(seq, OTA_STATUS_OK);
    s_sess.state = OTA_STATE_DONE;
}

static void on_frame(uint8_t seq, frame_type_t type,
                     const uint8_t *payload, size_t len, void *user)
{
    (void)user;

    /* Duplicate-SEQ idempotence: re-ACK without re-doing the work. The
     * sliding-window-1 link guarantees the host blocks on each ACK before
     * sending the next frame, so a duplicate here always means a missed
     * ACK on the previous round. */
    if (s_sess.have_last_rx_seq && seq == s_sess.last_rx_seq) {
        switch (type) {
        case FRAME_TYPE_OTA_BEGIN:
        case FRAME_TYPE_OTA_CHUNK:
            send_ack(seq);
            break;
        case FRAME_TYPE_PING:
            send_pong(seq);
            break;
        case FRAME_TYPE_OTA_END:
            /* Re-send last status after OTA_END would already have run
             * the final verify; but state is HALT/DONE so we just NACK. */
            send_nack(seq);
            break;
        default:
            send_nack(seq);
            break;
        }
        return;
    }

    switch (type) {
    case FRAME_TYPE_PING:
        send_pong(seq);
        break;

    case FRAME_TYPE_OTA_BEGIN:
        handle_ota_begin(payload, len, seq);
        break;

    case FRAME_TYPE_OTA_CHUNK:
        handle_ota_chunk(payload, len, seq);
        break;

    case FRAME_TYPE_OTA_END:
        handle_ota_end(seq);
        break;

    default:
        send_nack(seq);
        break;
    }

    s_sess.last_rx_seq      = seq;
    s_sess.have_last_rx_seq = 1u;
}

/* ------------------------------------------------------------------------
 * Entry point
 * ------------------------------------------------------------------------ */

void bootloader_ota_run(void)
{
    /* Slow LED blink visible from across the room — operator can tell
     * the chip is in OTA mode rather than running an app. */
    led2_init();
    led2_on();

    /*
     * uart_init() arms three USART2 interrupt sources: RXNEIE (per byte),
     * IDLEIE (when the line goes idle after activity), and CR3_EIE
     * (overrun/framing/noise).  All three race the polled OTA loop:
     *
     *   - RXNEIE: handler reads DR, clears RXNE, drops the byte (no
     *     callback registered).  uart_read() then spins forever.
     *   - IDLEIE: handler reads DR to clear the flag, which steals
     *     whatever byte was sitting in the receive register at that
     *     instant.  Inter-byte gaps inside a single frame on USB-CDC
     *     are enough to trigger this and corrupt the decoder.
     *   - EIE / ORE: same — handler reads DR to clear, byte gone.
     *
     * The OTA receiver wants none of that; it owns the bus end-to-end
     * via uart_read().  Disable all three before the polled loop and
     * mask the NVIC line so the ISR cannot run at all.  Drain any
     * byte that already raced in before the disable took effect.
     */
    USART2->CR1 &= ~(USART_CR1_RXNEIE | USART_CR1_IDLEIE);
    USART2->CR3 &= ~USART_CR3_EIE;
    NVIC_DisableIRQ(USART2_IRQn);
    if (USART2->SR & USART_SR_RXNE) {
        (void)USART2->DR;
    }

    memset(&s_sess, 0, sizeof(s_sess));
    s_sess.prev_active      = scan_active_slot();
    s_sess.max_seen_counter = scan_max_monotonic();
    s_sess.state            = OTA_STATE_AWAIT_BEGIN;

    uart_puts("OTA: ready\r\n");

    frame_decoder_t dec;
    if (frame_decoder_init(&dec, on_frame, NULL, NULL,
                           s_decoder_payload,
                           sizeof(s_decoder_payload)) != FRAME_OK) {
        uart_puts("OTA: decoder init failed\r\n");
        return;
    }

    while (s_sess.state != OTA_STATE_DONE && s_sess.state != OTA_STATE_HALT) {
        char c = uart_read();
        uint8_t b = (uint8_t)c;
        frame_decoder_feed(&dec, &b, 1);
    }

    if (s_sess.state == OTA_STATE_DONE) {
        /* Drain any pending TX bytes before resetting. uart_write is
         * blocking-byte-copy so by the time send_status() returned the
         * last byte was already on the wire — the small NOP burst is
         * just defensive. */
        for (volatile uint32_t i = 0; i < 200000u; ++i) {
            __asm volatile ("nop");
        }
        NVIC_SystemReset();
    }
    /* HALT: fall through; main.c's caller will halt with a slow blink. */
}
