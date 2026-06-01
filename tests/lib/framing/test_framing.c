#include "framing.h"
#include "unity.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ------------------------------------------------------------------------
 * Common test fixtures
 * ------------------------------------------------------------------------ */

#define MAX_CAPTURED 8

typedef struct {
    uint8_t      seq;
    frame_type_t type;
    size_t       len;
    uint8_t      payload[FRAME_MAX_PAYLOAD];
} captured_frame_t;

typedef struct {
    captured_frame_t frames[MAX_CAPTURED];
    size_t           count;
    frame_err_t      last_err;
    int              err_count;
} capture_t;

static void capture_rx_cb(uint8_t seq, frame_type_t type,
                          const uint8_t *payload, size_t len, void *user)
{
    capture_t *c = (capture_t *)user;
    if (c->count >= MAX_CAPTURED) {
        return;
    }
    captured_frame_t *f = &c->frames[c->count++];
    f->seq  = seq;
    f->type = type;
    f->len  = len;
    if (len > 0) {
        memcpy(f->payload, payload, len);
    }
}

static void capture_err_cb(frame_err_t err, void *user)
{
    capture_t *c = (capture_t *)user;
    c->last_err = err;
    c->err_count++;
}

/* ------------------------------------------------------------------------
 * frame_crc16 — known vectors
 * ------------------------------------------------------------------------ */

void test_crc16_known_vector_123456789(void)
{
    /* CRC-16/CCITT-FALSE("123456789") == 0x29B1 */
    const uint8_t v[] = "123456789";
    TEST_ASSERT_EQUAL_HEX16(0x29B1u, frame_crc16(v, sizeof(v) - 1));
}

void test_crc16_empty_input_is_init(void)
{
    /* Init value 0xFFFF is returned for length 0. */
    const uint8_t v[1] = {0};
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, frame_crc16(v, 0));
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, frame_crc16(NULL, 0));
}

/* ------------------------------------------------------------------------
 * Encoder happy path
 * ------------------------------------------------------------------------ */

void test_encode_empty_payload(void)
{
    uint8_t out[FRAME_MAX_ENCODED_SIZE];
    int n = frame_encode(0x42, FRAME_TYPE_PING, NULL, 0, out, sizeof(out));

    /* FLAG + seq + type + len_lo + len_hi + crc_lo + crc_hi + FLAG = 8 bytes
     * with no stuffing required (none of those bytes are FLAG/ESC for the
     * specific seq/type/CRC chosen). */
    TEST_ASSERT_GREATER_OR_EQUAL_INT(8, n);
    TEST_ASSERT_EQUAL_HEX8(FRAME_FLAG, out[0]);
    TEST_ASSERT_EQUAL_HEX8(FRAME_FLAG, out[n - 1]);
    TEST_ASSERT_EQUAL_HEX8(0x42, out[1]);
    TEST_ASSERT_EQUAL_HEX8(FRAME_TYPE_PING, out[2]);
}

void test_encode_then_decode_roundtrip(void)
{
    const uint8_t payload[] = {0x01, 0x02, 0x03, 0xA5, 0xFF};
    uint8_t out[FRAME_MAX_ENCODED_SIZE];
    int n = frame_encode(7, FRAME_TYPE_DATA,
                         payload, sizeof(payload),
                         out, sizeof(out));
    TEST_ASSERT_GREATER_THAN_INT(0, n);

    capture_t cap = {0};
    frame_decoder_t d;
    uint8_t pbuf[FRAME_MAX_PAYLOAD];
    TEST_ASSERT_EQUAL_INT(FRAME_OK,
        frame_decoder_init(&d, capture_rx_cb, capture_err_cb, &cap,
                           pbuf, sizeof(pbuf)));

    frame_decoder_feed(&d, out, (size_t)n);

    TEST_ASSERT_EQUAL_size_t(1u, cap.count);
    TEST_ASSERT_EQUAL_INT(0, cap.err_count);
    TEST_ASSERT_EQUAL_HEX8(7, cap.frames[0].seq);
    TEST_ASSERT_EQUAL_INT(FRAME_TYPE_DATA, cap.frames[0].type);
    TEST_ASSERT_EQUAL_size_t(sizeof(payload), cap.frames[0].len);
    TEST_ASSERT_EQUAL_MEMORY(payload, cap.frames[0].payload, sizeof(payload));
}

/* ------------------------------------------------------------------------
 * Byte-stuffing — payloads containing FLAG / ESC must round-trip cleanly
 * ------------------------------------------------------------------------ */

void test_encode_stuffs_flag_and_esc_in_payload(void)
{
    const uint8_t payload[] = {FRAME_FLAG, 0x00, FRAME_ESC, 0x55, FRAME_FLAG};
    uint8_t out[FRAME_MAX_ENCODED_SIZE];
    int n = frame_encode(1, FRAME_TYPE_DATA,
                         payload, sizeof(payload),
                         out, sizeof(out));
    TEST_ASSERT_GREATER_THAN_INT(0, n);

    /* Only the leading and trailing FLAG bytes should be FRAME_FLAG; every
     * stuffed FLAG/ESC inside the body must have been escaped. */
    int interior_flags = 0;
    for (int i = 1; i < n - 1; ++i) {
        if (out[i] == FRAME_FLAG) {
            interior_flags++;
        }
    }
    TEST_ASSERT_EQUAL_INT(0, interior_flags);

    /* Round-trip restores the original payload. */
    capture_t cap = {0};
    frame_decoder_t d;
    uint8_t pbuf[FRAME_MAX_PAYLOAD];
    frame_decoder_init(&d, capture_rx_cb, capture_err_cb, &cap,
                       pbuf, sizeof(pbuf));
    frame_decoder_feed(&d, out, (size_t)n);

    TEST_ASSERT_EQUAL_size_t(1u, cap.count);
    TEST_ASSERT_EQUAL_size_t(sizeof(payload), cap.frames[0].len);
    TEST_ASSERT_EQUAL_MEMORY(payload, cap.frames[0].payload, sizeof(payload));
}

void test_encode_stuffs_seq_when_seq_is_flag(void)
{
    /* SEQ field = 0x7E — must be stuffed too. */
    uint8_t out[FRAME_MAX_ENCODED_SIZE];
    int n = frame_encode(FRAME_FLAG, FRAME_TYPE_DATA, NULL, 0,
                         out, sizeof(out));
    TEST_ASSERT_GREATER_THAN_INT(0, n);

    /* Bytes 1+2 should be ESC, FLAG^0x20. */
    TEST_ASSERT_EQUAL_HEX8(FRAME_ESC,                 out[1]);
    TEST_ASSERT_EQUAL_HEX8(FRAME_FLAG ^ FRAME_ESC_XOR, out[2]);

    capture_t cap = {0};
    frame_decoder_t d;
    uint8_t pbuf[16];
    frame_decoder_init(&d, capture_rx_cb, capture_err_cb, &cap, pbuf, sizeof(pbuf));
    frame_decoder_feed(&d, out, (size_t)n);
    TEST_ASSERT_EQUAL_size_t(1u, cap.count);
    TEST_ASSERT_EQUAL_HEX8(FRAME_FLAG, cap.frames[0].seq);
}

/* ------------------------------------------------------------------------
 * Encoder error paths
 * ------------------------------------------------------------------------ */

void test_encode_null_out_buf_rejected(void)
{
    int n = frame_encode(0, FRAME_TYPE_DATA, NULL, 0, NULL, 16);
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_NULL_ARG, n);
}

void test_encode_null_payload_with_nonzero_len_rejected(void)
{
    uint8_t out[16];
    int n = frame_encode(0, FRAME_TYPE_DATA, NULL, 5, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_NULL_ARG, n);
}

void test_encode_oversize_payload_rejected(void)
{
    static uint8_t big[FRAME_MAX_PAYLOAD + 1];
    uint8_t out[FRAME_MAX_ENCODED_SIZE];
    int n = frame_encode(0, FRAME_TYPE_DATA, big, sizeof(big),
                         out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_OVERSIZE, n);
}

void test_encode_bad_type_rejected(void)
{
    uint8_t out[16];
    int n = frame_encode(0, (frame_type_t)(FRAME_TYPE__MAX + 1),
                         NULL, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_BAD_TYPE, n);
}

void test_encode_buf_too_small_rejected(void)
{
    uint8_t payload[8] = {0};
    uint8_t out[6]; /* far below worst-case for an 8-byte payload */
    int n = frame_encode(0, FRAME_TYPE_DATA, payload, sizeof(payload),
                         out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_BUF_SMALL, n);
}

/* ------------------------------------------------------------------------
 * Decoder error / edge cases
 * ------------------------------------------------------------------------ */

void test_decoder_init_null_args(void)
{
    frame_decoder_t d;
    uint8_t pbuf[8];
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_NULL_ARG,
        frame_decoder_init(NULL, capture_rx_cb, NULL, NULL, pbuf, sizeof(pbuf)));
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_NULL_ARG,
        frame_decoder_init(&d, NULL, NULL, NULL, pbuf, sizeof(pbuf)));
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_NULL_ARG,
        frame_decoder_init(&d, capture_rx_cb, NULL, NULL, NULL, 0));
}

void test_decoder_init_oversize_buf_rejected(void)
{
    frame_decoder_t d;
    /* size + 1 confirms the cap is exclusive of FRAME_MAX_PAYLOAD+1. */
    static uint8_t pbuf[FRAME_MAX_PAYLOAD + 1];
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_OVERSIZE,
        frame_decoder_init(&d, capture_rx_cb, NULL, NULL,
                           pbuf, sizeof(pbuf)));
}

void test_decoder_feed_null_args_safe(void)
{
    frame_decoder_t d;
    uint8_t pbuf[8];
    capture_t cap = {0};
    frame_decoder_init(&d, capture_rx_cb, capture_err_cb, &cap,
                       pbuf, sizeof(pbuf));

    /* Either NULL pointer must early-return without crashing. */
    frame_decoder_feed(NULL, NULL, 0);
    frame_decoder_feed(&d, NULL, 0);
    TEST_ASSERT_EQUAL_size_t(0u, cap.count);
}

void test_decoder_crc_mismatch_drops_frame_and_reports_err(void)
{
    const uint8_t payload[] = {0xCA, 0xFE};
    uint8_t out[FRAME_MAX_ENCODED_SIZE];
    int n = frame_encode(2, FRAME_TYPE_DATA, payload, sizeof(payload),
                         out, sizeof(out));
    TEST_ASSERT_GREATER_THAN_INT(0, n);

    /* Flip a single payload byte — CRC must reject. The mutated byte must not
     * become a special FLAG/ESC sentinel; XOR with 0x01 keeps 0xCA -> 0xCB. */
    out[5] ^= 0x01u;

    capture_t cap = {0};
    frame_decoder_t d;
    uint8_t pbuf[FRAME_MAX_PAYLOAD];
    frame_decoder_init(&d, capture_rx_cb, capture_err_cb, &cap, pbuf, sizeof(pbuf));
    frame_decoder_feed(&d, out, (size_t)n);

    TEST_ASSERT_EQUAL_size_t(0u, cap.count);
    TEST_ASSERT_EQUAL_INT(1, cap.err_count);
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_CRC, cap.last_err);
}

void test_decoder_truncation_then_resync(void)
{
    const uint8_t payload[] = {0xDE, 0xAD};
    uint8_t out[FRAME_MAX_ENCODED_SIZE];
    int n = frame_encode(5, FRAME_TYPE_DATA, payload, sizeof(payload),
                         out, sizeof(out));
    TEST_ASSERT_GREATER_THAN_INT(0, n);

    capture_t cap = {0};
    frame_decoder_t d;
    uint8_t pbuf[FRAME_MAX_PAYLOAD];
    frame_decoder_init(&d, capture_rx_cb, capture_err_cb, &cap, pbuf, sizeof(pbuf));

    /* Half a frame — decoder stays mid-frame, no callback fires. */
    frame_decoder_feed(&d, out, (size_t)(n / 2));
    TEST_ASSERT_EQUAL_size_t(0u, cap.count);

    /* Now feed a fresh, complete frame. The truncated half-frame must be
     * abandoned (closing FLAG of a partial body fires err_cb), and the new
     * frame must decode cleanly. */
    int n2 = frame_encode(6, FRAME_TYPE_PING, NULL, 0, out, sizeof(out));
    frame_decoder_feed(&d, out, (size_t)n2);

    TEST_ASSERT_EQUAL_size_t(1u, cap.count);
    TEST_ASSERT_EQUAL_HEX8(6, cap.frames[0].seq);
    TEST_ASSERT_EQUAL_INT(FRAME_TYPE_PING, cap.frames[0].type);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(1, cap.err_count);
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_TRUNC, cap.last_err);
}

void test_decoder_garbage_prefix_silently_dropped(void)
{
    const uint8_t junk[] = {0x00, 0x55, 0xAA, 0x33}; /* before any FLAG */
    const uint8_t payload[] = {0xBE, 0xEF};
    uint8_t out[FRAME_MAX_ENCODED_SIZE];
    int n = frame_encode(11, FRAME_TYPE_DATA, payload, sizeof(payload),
                         out, sizeof(out));

    capture_t cap = {0};
    frame_decoder_t d;
    uint8_t pbuf[FRAME_MAX_PAYLOAD];
    frame_decoder_init(&d, capture_rx_cb, capture_err_cb, &cap, pbuf, sizeof(pbuf));

    frame_decoder_feed(&d, junk, sizeof(junk));
    TEST_ASSERT_EQUAL_INT(0, cap.err_count);

    frame_decoder_feed(&d, out, (size_t)n);
    TEST_ASSERT_EQUAL_size_t(1u, cap.count);
    TEST_ASSERT_EQUAL_INT(0, cap.err_count);
}

void test_decoder_idle_flag_pair_ignored(void)
{
    /* Two adjacent FLAGs (HDLC idle marker) must not produce a frame or err. */
    const uint8_t flags[] = {FRAME_FLAG, FRAME_FLAG, FRAME_FLAG};
    capture_t cap = {0};
    frame_decoder_t d;
    uint8_t pbuf[8];
    frame_decoder_init(&d, capture_rx_cb, capture_err_cb, &cap, pbuf, sizeof(pbuf));
    frame_decoder_feed(&d, flags, sizeof(flags));
    TEST_ASSERT_EQUAL_size_t(0u, cap.count);
    TEST_ASSERT_EQUAL_INT(0, cap.err_count);
}

void test_decoder_oversize_payload_dropped(void)
{
    /* Hand-craft a frame whose LEN field claims more bytes than the
     * decoder's payload buffer can hold. */
    uint8_t buf[64];
    size_t i = 0;
    buf[i++] = FRAME_FLAG;
    buf[i++] = 0x00;          /* seq */
    buf[i++] = FRAME_TYPE_DATA;
    buf[i++] = 0x10;          /* len_lo = 16 */
    buf[i++] = 0x00;          /* len_hi */
    /* No need to fill 16 payload bytes — the decoder must drop on the
     * field-complete check before consuming payload. */

    capture_t cap = {0};
    frame_decoder_t d;
    uint8_t pbuf[8]; /* < 16 advertised */
    frame_decoder_init(&d, capture_rx_cb, capture_err_cb, &cap, pbuf, sizeof(pbuf));
    frame_decoder_feed(&d, buf, i);
    TEST_ASSERT_EQUAL_size_t(0u, cap.count);
    TEST_ASSERT_EQUAL_INT(1, cap.err_count);
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_OVERSIZE, cap.last_err);
}

void test_decoder_double_esc_drops_frame(void)
{
    /* ESC followed immediately by another ESC is illegal stuffing. */
    const uint8_t bad[] = {FRAME_FLAG, FRAME_ESC, FRAME_ESC, FRAME_FLAG};
    capture_t cap = {0};
    frame_decoder_t d;
    uint8_t pbuf[8];
    frame_decoder_init(&d, capture_rx_cb, capture_err_cb, &cap, pbuf, sizeof(pbuf));
    frame_decoder_feed(&d, bad, sizeof(bad));
    TEST_ASSERT_EQUAL_size_t(0u, cap.count);
    TEST_ASSERT_EQUAL_INT(1, cap.err_count);
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_TRUNC, cap.last_err);
}

void test_decoder_trailing_esc_before_flag_drops_frame(void)
{
    /* A frame whose body ends with a lone ESC immediately before the closing
     * FLAG is malformed (the ESC must be followed by the XORed byte). */
    const uint8_t bad[] = {FRAME_FLAG, 0x01, 0x02, 0x00, 0x00, FRAME_ESC, FRAME_FLAG};
    capture_t cap = {0};
    frame_decoder_t d;
    uint8_t pbuf[8];
    frame_decoder_init(&d, capture_rx_cb, capture_err_cb, &cap, pbuf, sizeof(pbuf));
    frame_decoder_feed(&d, bad, sizeof(bad));
    TEST_ASSERT_EQUAL_size_t(0u, cap.count);
    TEST_ASSERT_EQUAL_INT(1, cap.err_count);
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_TRUNC, cap.last_err);
}

void test_decoder_bad_type_with_valid_crc_reports_err(void)
{
    /* Build a frame that has a CRC-valid body but TYPE > FRAME_TYPE__MAX.
     * Easiest: encode normally then mutate the type byte and recompute CRC
     * by hand. We instead just bypass the encoder and stuff manually. */
    const uint8_t header[4] = {0x00, 0xFF /* bogus type */, 0x00, 0x00};
    uint16_t crc = frame_crc16(header, sizeof(header));

    uint8_t buf[16];
    size_t i = 0;
    buf[i++] = FRAME_FLAG;
    buf[i++] = header[0];
    buf[i++] = header[1];  /* not FLAG/ESC -> no stuffing */
    buf[i++] = header[2];
    buf[i++] = header[3];
    buf[i++] = (uint8_t)(crc & 0xFFu);
    buf[i++] = (uint8_t)((crc >> 8) & 0xFFu);
    buf[i++] = FRAME_FLAG;

    capture_t cap = {0};
    frame_decoder_t d;
    uint8_t pbuf[8];
    frame_decoder_init(&d, capture_rx_cb, capture_err_cb, &cap, pbuf, sizeof(pbuf));
    frame_decoder_feed(&d, buf, i);

    TEST_ASSERT_EQUAL_size_t(0u, cap.count);
    TEST_ASSERT_EQUAL_INT(1, cap.err_count);
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_BAD_TYPE, cap.last_err);
}

void test_decoder_byte_at_a_time_feed(void)
{
    const uint8_t payload[] = {0x10, 0x20, 0x30, 0x40};
    uint8_t out[FRAME_MAX_ENCODED_SIZE];
    int n = frame_encode(9, FRAME_TYPE_OTA_CHUNK,
                         payload, sizeof(payload),
                         out, sizeof(out));

    capture_t cap = {0};
    frame_decoder_t d;
    uint8_t pbuf[FRAME_MAX_PAYLOAD];
    frame_decoder_init(&d, capture_rx_cb, capture_err_cb, &cap, pbuf, sizeof(pbuf));

    /* Feed one byte at a time — the decoder must reassemble correctly. */
    for (int i = 0; i < n; ++i) {
        frame_decoder_feed(&d, &out[i], 1);
    }
    TEST_ASSERT_EQUAL_size_t(1u, cap.count);
    TEST_ASSERT_EQUAL_HEX8(9, cap.frames[0].seq);
    TEST_ASSERT_EQUAL_size_t(sizeof(payload), cap.frames[0].len);
}

void test_decoder_max_payload_round_trip(void)
{
    static uint8_t big[FRAME_MAX_PAYLOAD];
    for (size_t i = 0; i < sizeof(big); ++i) {
        big[i] = (uint8_t)(i & 0xFFu);
    }
    static uint8_t out[FRAME_MAX_ENCODED_SIZE];
    int n = frame_encode(0, FRAME_TYPE_DATA, big, sizeof(big),
                         out, sizeof(out));
    TEST_ASSERT_GREATER_THAN_INT(0, n);

    capture_t cap = {0};
    frame_decoder_t d;
    static uint8_t pbuf[FRAME_MAX_PAYLOAD];
    frame_decoder_init(&d, capture_rx_cb, capture_err_cb, &cap, pbuf, sizeof(pbuf));
    frame_decoder_feed(&d, out, (size_t)n);

    TEST_ASSERT_EQUAL_size_t(1u, cap.count);
    TEST_ASSERT_EQUAL_size_t(sizeof(big), cap.frames[0].len);
    TEST_ASSERT_EQUAL_MEMORY(big, cap.frames[0].payload, sizeof(big));
}

void test_decoder_reset_clears_in_progress_frame(void)
{
    capture_t cap = {0};
    frame_decoder_t d;
    uint8_t pbuf[8];
    frame_decoder_init(&d, capture_rx_cb, capture_err_cb, &cap, pbuf, sizeof(pbuf));

    /* Start a frame mid-stream, then reset before completing it. */
    const uint8_t partial[] = {FRAME_FLAG, 0x01, 0x02};
    frame_decoder_feed(&d, partial, sizeof(partial));
    frame_decoder_reset(&d);
    frame_decoder_reset(NULL); /* no-op, must not crash */

    /* Reset should also discard the carried in_frame=1, so a subsequent
     * fresh frame must decode without surfacing the partial as a TRUNC. */
    uint8_t out[FRAME_MAX_ENCODED_SIZE];
    int n = frame_encode(3, FRAME_TYPE_PING, NULL, 0, out, sizeof(out));
    frame_decoder_feed(&d, out, (size_t)n);

    TEST_ASSERT_EQUAL_size_t(1u, cap.count);
    TEST_ASSERT_EQUAL_HEX8(3, cap.frames[0].seq);
    /* No truncation error because the partial was reset, not finalised. */
    TEST_ASSERT_EQUAL_INT(0, cap.err_count);
}

/* ------------------------------------------------------------------------
 * Reliable layer (frame_link)
 * ------------------------------------------------------------------------ */

typedef struct {
    uint8_t  bytes[FRAME_MAX_ENCODED_SIZE];
    size_t   len;
    int      writes;
    int      write_should_fail;
    uint32_t now;
} link_io_t;

static int link_write_cb(const uint8_t *bytes, size_t n, void *user)
{
    link_io_t *io = (link_io_t *)user;
    if (io->write_should_fail) {
        return -1;
    }
    io->writes++;
    if (n > sizeof(io->bytes)) n = sizeof(io->bytes);
    memcpy(io->bytes, bytes, n);
    io->len = n;
    return 0;
}

static uint32_t link_now_ms_cb(void *user)
{
    link_io_t *io = (link_io_t *)user;
    return io->now;
}

void test_link_init_null_args(void)
{
    frame_link_t link;
    link_io_t io = {0};
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_NULL_ARG,
        frame_link_init(NULL, link_write_cb, link_now_ms_cb, &io, 100, 3));
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_NULL_ARG,
        frame_link_init(&link, NULL, link_now_ms_cb, &io, 100, 3));
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_NULL_ARG,
        frame_link_init(&link, link_write_cb, NULL, &io, 100, 3));
}

void test_link_send_then_ack_advances_seq(void)
{
    frame_link_t link;
    link_io_t io = {0};
    frame_link_init(&link, link_write_cb, link_now_ms_cb, &io, 100, 3);

    const uint8_t payload[] = {0xAB};
    TEST_ASSERT_EQUAL_INT(FRAME_OK,
        frame_link_send(&link, FRAME_TYPE_DATA, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_INT(1, io.writes);

    /* ACK with wrong seq is ignored. */
    TEST_ASSERT_EQUAL_INT(0, frame_link_on_ack(&link, 99));

    /* Correct ACK advances the link to IDLE; tx_seq increments. */
    TEST_ASSERT_EQUAL_INT(1, frame_link_on_ack(&link, 0));
    TEST_ASSERT_EQUAL_INT(0, frame_link_on_ack(&link, 0)); /* now IDLE */

    /* Next send uses seq 1. */
    TEST_ASSERT_EQUAL_INT(FRAME_OK,
        frame_link_send(&link, FRAME_TYPE_DATA, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_INT(1, frame_link_on_ack(&link, 1));
}

void test_link_send_overlap_rejected(void)
{
    frame_link_t link;
    link_io_t io = {0};
    frame_link_init(&link, link_write_cb, link_now_ms_cb, &io, 100, 3);

    const uint8_t payload[] = {0xAB};
    frame_link_send(&link, FRAME_TYPE_DATA, payload, sizeof(payload));
    /* Sliding-window-of-1: a second send while AWAITING_ACK must fail. */
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_BUF_SMALL,
        frame_link_send(&link, FRAME_TYPE_DATA, payload, sizeof(payload)));
}

void test_link_send_oversize_rejected(void)
{
    frame_link_t link;
    link_io_t io = {0};
    frame_link_init(&link, link_write_cb, link_now_ms_cb, &io, 100, 3);
    static uint8_t big[FRAME_MAX_PAYLOAD + 1];
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_OVERSIZE,
        frame_link_send(&link, FRAME_TYPE_DATA, big, sizeof(big)));
}

void test_link_send_null_link_rejected(void)
{
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_NULL_ARG,
        frame_link_send(NULL, FRAME_TYPE_DATA, NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, frame_link_on_ack(NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, frame_link_on_nack(NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, frame_link_tick(NULL));
    TEST_ASSERT_EQUAL_INT(0, frame_link_on_rx(NULL, 0));
}

void test_link_send_write_failure_keeps_state_idle(void)
{
    frame_link_t link;
    link_io_t io = {0};
    frame_link_init(&link, link_write_cb, link_now_ms_cb, &io, 100, 3);

    io.write_should_fail = 1;
    const uint8_t payload[] = {0xAB};
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_BUF_SMALL,
        frame_link_send(&link, FRAME_TYPE_DATA, payload, sizeof(payload)));

    /* State remained IDLE — caller can retry without overlap. */
    io.write_should_fail = 0;
    TEST_ASSERT_EQUAL_INT(FRAME_OK,
        frame_link_send(&link, FRAME_TYPE_DATA, payload, sizeof(payload)));
}

void test_link_nack_triggers_retransmit(void)
{
    frame_link_t link;
    link_io_t io = {0};
    frame_link_init(&link, link_write_cb, link_now_ms_cb, &io, 100, 2);

    const uint8_t payload[] = {0xAB};
    frame_link_send(&link, FRAME_TYPE_DATA, payload, sizeof(payload));
    TEST_ASSERT_EQUAL_INT(1, io.writes);

    /* First NACK -> retransmit attempt #2. */
    TEST_ASSERT_EQUAL_INT(1, frame_link_on_nack(&link, 0));
    TEST_ASSERT_EQUAL_INT(2, io.writes);

    /* Second NACK -> retransmit attempt #3 (last allowed: 1 + max_retries). */
    TEST_ASSERT_EQUAL_INT(1, frame_link_on_nack(&link, 0));
    TEST_ASSERT_EQUAL_INT(3, io.writes);

    /* Third NACK -> exhausted, link returns to IDLE. */
    TEST_ASSERT_EQUAL_INT(0, frame_link_on_nack(&link, 0));
}

void test_link_tick_retransmits_after_timeout(void)
{
    frame_link_t link;
    link_io_t io = {0};
    frame_link_init(&link, link_write_cb, link_now_ms_cb, &io, 100, 2);

    const uint8_t payload[] = {0xAB};
    frame_link_send(&link, FRAME_TYPE_DATA, payload, sizeof(payload));

    /* Tick before timeout -> nothing happens. */
    io.now = 50;
    TEST_ASSERT_EQUAL_INT(0, frame_link_tick(&link));
    TEST_ASSERT_EQUAL_INT(1, io.writes);

    /* Tick at timeout boundary -> retransmit. */
    io.now = 100;
    TEST_ASSERT_EQUAL_INT(1, frame_link_tick(&link));
    TEST_ASSERT_EQUAL_INT(2, io.writes);

    /* Two more ticks past their respective timeouts -> third attempt then exhausted. */
    io.now = 250;
    TEST_ASSERT_EQUAL_INT(1, frame_link_tick(&link));
    io.now = 400;
    TEST_ASSERT_EQUAL_INT(-1, frame_link_tick(&link));
}

void test_link_tick_idle_link_is_noop(void)
{
    frame_link_t link;
    link_io_t io = {0};
    frame_link_init(&link, link_write_cb, link_now_ms_cb, &io, 100, 2);
    /* Never sent — link is IDLE. */
    TEST_ASSERT_EQUAL_INT(0, frame_link_tick(&link));
}

void test_link_on_rx_dedups_repeated_seq(void)
{
    frame_link_t link;
    link_io_t io = {0};
    frame_link_init(&link, link_write_cb, link_now_ms_cb, &io, 100, 2);

    /* First time we've seen seq=42 -> fresh. */
    TEST_ASSERT_EQUAL_INT(1, frame_link_on_rx(&link, 42));
    /* Same seq again -> duplicate (caller still ACKs it). */
    TEST_ASSERT_EQUAL_INT(0, frame_link_on_rx(&link, 42));
    /* Different seq -> fresh again. */
    TEST_ASSERT_EQUAL_INT(1, frame_link_on_rx(&link, 43));
}

/* ------------------------------------------------------------------------
 * Test runner
 * ------------------------------------------------------------------------ */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_crc16_known_vector_123456789);
    RUN_TEST(test_crc16_empty_input_is_init);

    RUN_TEST(test_encode_empty_payload);
    RUN_TEST(test_encode_then_decode_roundtrip);
    RUN_TEST(test_encode_stuffs_flag_and_esc_in_payload);
    RUN_TEST(test_encode_stuffs_seq_when_seq_is_flag);

    RUN_TEST(test_encode_null_out_buf_rejected);
    RUN_TEST(test_encode_null_payload_with_nonzero_len_rejected);
    RUN_TEST(test_encode_oversize_payload_rejected);
    RUN_TEST(test_encode_bad_type_rejected);
    RUN_TEST(test_encode_buf_too_small_rejected);

    RUN_TEST(test_decoder_init_null_args);
    RUN_TEST(test_decoder_init_oversize_buf_rejected);
    RUN_TEST(test_decoder_feed_null_args_safe);
    RUN_TEST(test_decoder_crc_mismatch_drops_frame_and_reports_err);
    RUN_TEST(test_decoder_truncation_then_resync);
    RUN_TEST(test_decoder_garbage_prefix_silently_dropped);
    RUN_TEST(test_decoder_idle_flag_pair_ignored);
    RUN_TEST(test_decoder_oversize_payload_dropped);
    RUN_TEST(test_decoder_double_esc_drops_frame);
    RUN_TEST(test_decoder_trailing_esc_before_flag_drops_frame);
    RUN_TEST(test_decoder_bad_type_with_valid_crc_reports_err);
    RUN_TEST(test_decoder_byte_at_a_time_feed);
    RUN_TEST(test_decoder_max_payload_round_trip);
    RUN_TEST(test_decoder_reset_clears_in_progress_frame);

    RUN_TEST(test_link_init_null_args);
    RUN_TEST(test_link_send_then_ack_advances_seq);
    RUN_TEST(test_link_send_overlap_rejected);
    RUN_TEST(test_link_send_oversize_rejected);
    RUN_TEST(test_link_send_null_link_rejected);
    RUN_TEST(test_link_send_write_failure_keeps_state_idle);
    RUN_TEST(test_link_nack_triggers_retransmit);
    RUN_TEST(test_link_tick_retransmits_after_timeout);
    RUN_TEST(test_link_tick_idle_link_is_noop);
    RUN_TEST(test_link_on_rx_dedups_repeated_seq);

    return UNITY_END();
}
