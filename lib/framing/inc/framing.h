#ifndef LIB_FRAMING_H
#define LIB_FRAMING_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * HDLC-inspired framing for the bootloader OTA path (Plan 001 Phase 1.8) and
 * the inter-board comms link (Plan 002 Phase 2.2).
 *
 * Wire format (between two FLAG bytes, the inner span is byte-stuffed):
 *
 *   [FLAG] [SEQ] [TYPE] [LEN_lo] [LEN_hi] [PAYLOAD ...] [CRC_lo] [CRC_hi] [FLAG]
 *
 * - FLAG = 0x7E, ESC = 0x7D. Any FLAG/ESC byte inside the frame body is
 *   replaced by ESC followed by (byte ^ 0x20). The decoder reverses the
 *   substitution before CRC checking.
 * - CRC is CRC-16-CCITT (poly 0x1021, init 0xFFFF, no final XOR) computed over
 *   the unstuffed [SEQ][TYPE][LEN_lo][LEN_hi][PAYLOAD] span.
 * - Maximum payload is FRAME_MAX_PAYLOAD bytes; larger transfers chunk over
 *   multiple frames at the application layer.
 *
 * Both encoder and decoder are pure functions / pure state machines. They do
 * not touch any peripheral and therefore work identically on the host (for
 * unit tests and `tools/ota_send.py`) and on the target.
 */

#define FRAME_FLAG               0x7Eu
#define FRAME_ESC                0x7Du
#define FRAME_ESC_XOR            0x20u

#define FRAME_MAX_PAYLOAD        1024u

/* Bytes occupied by the [SEQ][TYPE][LEN_lo][LEN_hi][CRC_lo][CRC_hi] fields. */
#define FRAME_FIXED_OVERHEAD     6u

/*
 * Worst-case encoded size: every byte in the body region (overhead + payload)
 * may need an ESC prefix, plus the two FLAG sentinels.
 *
 *   2 (flags) + 2 * (FRAME_FIXED_OVERHEAD + payload_len)
 */
#define FRAME_MAX_ENCODED_SIZE                                                \
    (2u + 2u * (FRAME_FIXED_OVERHEAD + FRAME_MAX_PAYLOAD))

typedef enum {
    FRAME_OK             =  0,
    FRAME_ERR_NULL_ARG   = -1,
    FRAME_ERR_OVERSIZE   = -2,  /* payload_len > FRAME_MAX_PAYLOAD */
    FRAME_ERR_BUF_SMALL  = -3,  /* out_cap insufficient for worst-case stuffing */
    FRAME_ERR_BAD_TYPE   = -4,
    FRAME_ERR_CRC        = -5,  /* decoder: CRC mismatch (frame dropped) */
    FRAME_ERR_TRUNC      = -6,  /* decoder: frame ended before LEN bytes consumed */
} frame_err_t;

typedef enum {
    FRAME_TYPE_DATA       = 0,
    FRAME_TYPE_ACK        = 1,
    FRAME_TYPE_NACK       = 2,
    FRAME_TYPE_OTA_BEGIN  = 3,
    FRAME_TYPE_OTA_CHUNK  = 4,
    FRAME_TYPE_OTA_END    = 5,
    FRAME_TYPE_PING       = 6,
    FRAME_TYPE_PONG       = 7,
    FRAME_TYPE_STATUS     = 8,
    FRAME_TYPE__MAX       = 8,  /* highest valid value, inclusive */
} frame_type_t;

/*
 * CRC-16-CCITT, poly 0x1021, init 0xFFFF, no final XOR.
 * Software-only; the STM32F4 hardware CRC engine implements CRC-32 with a
 * fixed polynomial and cannot accelerate this.
 */
uint16_t frame_crc16(const uint8_t *buf, size_t len);

/*
 * Encode (seq, type, payload[0..payload_len)) into out_buf using HDLC-style
 * framing with byte-stuffing and CRC-16-CCITT. Returns the number of bytes
 * written into out_buf on success, or a negative frame_err_t.
 *
 * Failure modes:
 *   - NULL out_buf, or NULL payload with payload_len > 0  -> FRAME_ERR_NULL_ARG
 *   - payload_len > FRAME_MAX_PAYLOAD                     -> FRAME_ERR_OVERSIZE
 *   - type > FRAME_TYPE__MAX                              -> FRAME_ERR_BAD_TYPE
 *   - out_cap < worst-case encoded size for this payload  -> FRAME_ERR_BUF_SMALL
 */
int frame_encode(uint8_t seq, frame_type_t type,
                 const uint8_t *payload, size_t payload_len,
                 uint8_t *out_buf, size_t out_cap);

/*
 * Stateful decoder. The caller hands the decoder an external payload buffer at
 * init time; the decoder reassembles each frame in place there. Whenever a
 * complete, CRC-valid frame has been reconstructed, the registered callback
 * fires with the decoded fields. On CRC mismatch or oversize input the frame
 * is silently dropped and the decoder waits for the next FLAG to resync; the
 * caller is also notified through the optional error callback so the higher
 * layer can NACK or count drops.
 */

typedef void (*frame_rx_cb_t)(uint8_t seq, frame_type_t type,
                              const uint8_t *payload, size_t len,
                              void *user);

typedef void (*frame_err_cb_t)(frame_err_t err, void *user);

typedef struct {
    /* Caller-owned callbacks + opaque user pointer. err_cb may be NULL. */
    frame_rx_cb_t  rx_cb;
    frame_err_cb_t err_cb;
    void          *user;

    /* Caller-owned reassembly buffer. */
    uint8_t *payload_buf;
    size_t   payload_buf_size;

    /*
     * Internal state. Treat as opaque from outside the module — exposed only
     * so callers can stack-allocate the struct.
     *
     * field_idx counts bytes received in the [SEQ][TYPE][LEN_lo][LEN_hi]
     * prefix; once it reaches 4 we switch to payload accumulation, then to
     * CRC bytes after LEN bytes have been seen.
     */
    uint8_t  seq;
    uint8_t  type;
    uint16_t declared_len;
    uint16_t payload_idx;
    uint8_t  crc_bytes[2];
    uint8_t  crc_idx;
    uint8_t  field_idx;        /* 0..4 for header bytes */

    uint8_t  in_frame;         /* 0 = idle, 1 = inside FLAG..FLAG */
    uint8_t  in_escape;        /* last byte was ESC, next is XORed */
    uint8_t  dropping;         /* current frame is poisoned, drop until FLAG */
} frame_decoder_t;

/*
 * Wire up a decoder to its reassembly buffer and callbacks. The buffer must
 * be at least FRAME_MAX_PAYLOAD bytes if you want to handle the largest
 * frame type; smaller buffers cause oversized frames to be dropped (and an
 * error reported via err_cb if registered).
 *
 * Returns FRAME_OK on success, FRAME_ERR_NULL_ARG if d/rx_cb/payload_buf is
 * NULL, FRAME_ERR_OVERSIZE if payload_buf_size > FRAME_MAX_PAYLOAD (we
 * intentionally cap the on-the-wire receive buffer at the protocol max so a
 * caller that mis-sizes their buffer doesn't accidentally accept oversized
 * frames).
 */
frame_err_t frame_decoder_init(frame_decoder_t *d,
                               frame_rx_cb_t rx_cb,
                               frame_err_cb_t err_cb,
                               void *user,
                               uint8_t *payload_buf,
                               size_t payload_buf_size);

/* Reset the decoder back to idle without changing its callbacks/buffer. */
void frame_decoder_reset(frame_decoder_t *d);

/* Feed n bytes into the decoder. Callbacks may fire during this call. */
void frame_decoder_feed(frame_decoder_t *d, const uint8_t *bytes, size_t n);

/* ------------------------------------------------------------------------
 * Reliable layer (sliding window of 1)
 *
 * Wraps the encoder/decoder with stop-and-wait ARQ:
 *  - send() encodes a DATA frame and starts a timeout
 *  - on ACK with matching SEQ -> success, advance window
 *  - on NACK or timeout       -> retransmit, up to max_retries
 *  - duplicate SEQ on receive -> drop payload, ACK anyway (idempotent)
 *  - bad CRC on receive       -> NACK with last good SEQ
 *
 * The link is transport-agnostic: the caller supplies a write callback that
 * pushes encoded bytes onto whatever transport (UART/SPI/socket) is in use,
 * plus a monotonic millisecond clock.
 * ------------------------------------------------------------------------ */

typedef int (*frame_link_write_cb_t)(const uint8_t *bytes, size_t n, void *user);
typedef uint32_t (*frame_link_now_ms_cb_t)(void *user);

typedef enum {
    FRAME_LINK_IDLE        = 0,
    FRAME_LINK_AWAITING_ACK = 1,
} frame_link_state_t;

typedef struct {
    /* Transport hooks. */
    frame_link_write_cb_t  write;
    frame_link_now_ms_cb_t now_ms;
    void                  *user;

    /* Tunables. */
    uint32_t timeout_ms;     /* how long to wait for ACK before retransmit */
    uint8_t  max_retries;    /* total send attempts = 1 + max_retries */

    /* Sender state. */
    frame_link_state_t state;
    uint8_t  tx_seq;
    uint32_t tx_started_ms;
    uint8_t  attempts;
    uint8_t  pending_type;
    uint16_t pending_len;
    uint8_t  pending_payload[FRAME_MAX_PAYLOAD];
    uint8_t  encoded[FRAME_MAX_ENCODED_SIZE];
    size_t   encoded_len;

    /* Receiver state (for duplicate suppression / NACK building). */
    uint8_t  rx_have_last;
    uint8_t  rx_last_seq;
} frame_link_t;

/*
 * Initialise a link with the given transport hooks and tunables. Sets the
 * initial sender SEQ to 0; the decoder side has no SEQ until the first frame
 * arrives.
 */
frame_err_t frame_link_init(frame_link_t *link,
                            frame_link_write_cb_t write_cb,
                            frame_link_now_ms_cb_t now_ms_cb,
                            void *user,
                            uint32_t timeout_ms,
                            uint8_t max_retries);

/*
 * Send a DATA frame and start the ARQ window. Returns FRAME_OK on success,
 * an encoder error otherwise. Caller must drive frame_link_tick() periodically
 * (or from the byte stream) to handle timeouts and retransmits.
 *
 * Returns FRAME_ERR_BUF_SMALL if a previous send is still awaiting ACK — the
 * sliding window of 1 means the caller must wait for that send to resolve.
 */
frame_err_t frame_link_send(frame_link_t *link,
                            frame_type_t type,
                            const uint8_t *payload, size_t payload_len);

/*
 * Notify the link that an ACK was received for `seq`. If it matches the
 * pending SEQ, the link advances to IDLE. Mismatched ACKs are ignored.
 * Returns 1 if the ACK matched (caller can issue another send), 0 otherwise.
 */
int frame_link_on_ack(frame_link_t *link, uint8_t seq);

/*
 * Notify the link that a NACK was received. Triggers an immediate
 * retransmit if attempts remain. Returns 1 if a retransmit was issued, 0 if
 * max retries were exhausted (caller should treat the send as failed).
 */
int frame_link_on_nack(frame_link_t *link, uint8_t seq);

/*
 * Periodic timer: call this regularly to honour the ACK timeout. Returns:
 *   1 if a retransmit was just issued
 *   0 if there was nothing to do (idle or still within timeout)
 *  -1 if the send has now permanently failed (max_retries exhausted)
 */
int frame_link_tick(frame_link_t *link);

/*
 * Receive helper. Call this from the decoder's rx_cb to apply duplicate
 * suppression: returns 1 if the frame is fresh and should be processed,
 * 0 if it duplicates the previous SEQ (caller should still ACK it).
 */
int frame_link_on_rx(frame_link_t *link, uint8_t seq);

#ifdef __cplusplus
}
#endif

#endif /* LIB_FRAMING_H */
