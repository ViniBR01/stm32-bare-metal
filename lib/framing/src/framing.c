#include "framing.h"

#include <string.h>

/* ------------------------------------------------------------------------
 * CRC-16-CCITT (a.k.a. CRC-16/IBM-3740, "CCITT-FALSE")
 *   poly = 0x1021, init = 0xFFFF, refin = refout = false, xorout = 0x0000
 *   reference vector: crc16("123456789") == 0x29B1
 *
 * Bytewise (no precomputed table) — the framing layer is bandwidth-bound at
 * 115200 baud and the CRC is computed over <= ~1030 bytes per frame, so the
 * extra cycles relative to a 256-entry table are well below the per-byte
 * UART time and the table would cost us 512 B of bootloader flash.
 * ------------------------------------------------------------------------ */
uint16_t frame_crc16(const uint8_t *buf, size_t len)
{
    uint16_t crc = 0xFFFFu;
    if (buf == NULL) {
        return crc;
    }
    for (size_t i = 0; i < len; ++i) {
        crc ^= ((uint16_t)buf[i]) << 8;
        for (int b = 0; b < 8; ++b) {
            if (crc & 0x8000u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

/* ------------------------------------------------------------------------
 * Encoder
 * ------------------------------------------------------------------------ */

/*
 * Append `b` to out_buf, byte-stuffing if necessary. Returns 0 on success,
 * -1 if there is no room in out_buf.
 */
static int emit_stuffed(uint8_t b, uint8_t *out_buf, size_t out_cap, size_t *idx)
{
    if (b == FRAME_FLAG || b == FRAME_ESC) {
        if (*idx + 2 > out_cap) {
            return -1;
        }
        out_buf[(*idx)++] = FRAME_ESC;
        out_buf[(*idx)++] = (uint8_t)(b ^ FRAME_ESC_XOR);
    } else {
        if (*idx + 1 > out_cap) {
            return -1;
        }
        out_buf[(*idx)++] = b;
    }
    return 0;
}

int frame_encode(uint8_t seq, frame_type_t type,
                 const uint8_t *payload, size_t payload_len,
                 uint8_t *out_buf, size_t out_cap)
{
    if (out_buf == NULL || (payload == NULL && payload_len > 0)) {
        return FRAME_ERR_NULL_ARG;
    }
    if (payload_len > FRAME_MAX_PAYLOAD) {
        return FRAME_ERR_OVERSIZE;
    }
    if ((unsigned)type > (unsigned)FRAME_TYPE__MAX) {
        return FRAME_ERR_BAD_TYPE;
    }

    /*
     * Worst-case encoded size for this payload: 2 FLAG bytes + every byte in
     * the body region (4 header + payload + 2 CRC) potentially doubled by
     * stuffing. Reject up-front so the caller doesn't have to know the
     * formula.
     */
    const size_t worst_case =
        2u + 2u * (FRAME_FIXED_OVERHEAD + payload_len);
    if (out_cap < worst_case) {
        return FRAME_ERR_BUF_SMALL;
    }

    /* Header: SEQ, TYPE, LEN_lo, LEN_hi. */
    const uint8_t header[4] = {
        seq,
        (uint8_t)type,
        (uint8_t)(payload_len & 0xFFu),
        (uint8_t)((payload_len >> 8) & 0xFFu),
    };

    /*
     * CRC covers [SEQ][TYPE][LEN_lo][LEN_hi][PAYLOAD] — the unstuffed body,
     * NOT including FLAGs or the CRC bytes themselves.
     */
    uint16_t crc = frame_crc16(header, sizeof(header));
    /* Continue the CRC over the payload without recomputing the header. */
    if (payload_len > 0) {
        /*
         * frame_crc16 reinitialises crc to 0xFFFF, so we re-implement the
         * inner loop here over (header || payload) by combining a buffer.
         * For modest payloads we just CRC over a stack copy; for full-sized
         * payloads (1024 B) that would blow the small bootloader stack, so
         * we instead resume the CRC manually.
         */
        for (size_t i = 0; i < payload_len; ++i) {
            crc ^= ((uint16_t)payload[i]) << 8;
            for (int b = 0; b < 8; ++b) {
                if (crc & 0x8000u) {
                    crc = (uint16_t)((crc << 1) ^ 0x1021u);
                } else {
                    crc = (uint16_t)(crc << 1);
                }
            }
        }
    }

    size_t idx = 0;
    out_buf[idx++] = FRAME_FLAG;

    for (size_t i = 0; i < sizeof(header); ++i) {
        if (emit_stuffed(header[i], out_buf, out_cap, &idx) != 0) {
            return FRAME_ERR_BUF_SMALL;
        }
    }
    for (size_t i = 0; i < payload_len; ++i) {
        if (emit_stuffed(payload[i], out_buf, out_cap, &idx) != 0) {
            return FRAME_ERR_BUF_SMALL;
        }
    }

    /* CRC on the wire is little-endian: low byte first, high byte second. */
    if (emit_stuffed((uint8_t)(crc & 0xFFu),        out_buf, out_cap, &idx) != 0) {
        return FRAME_ERR_BUF_SMALL;
    }
    if (emit_stuffed((uint8_t)((crc >> 8) & 0xFFu), out_buf, out_cap, &idx) != 0) {
        return FRAME_ERR_BUF_SMALL;
    }

    if (idx + 1 > out_cap) {
        return FRAME_ERR_BUF_SMALL;
    }
    out_buf[idx++] = FRAME_FLAG;

    return (int)idx;
}

/* ------------------------------------------------------------------------
 * Decoder
 * ------------------------------------------------------------------------ */

static void decoder_clear_frame_state(frame_decoder_t *d)
{
    d->seq          = 0;
    d->type         = 0;
    d->declared_len = 0;
    d->payload_idx  = 0;
    d->crc_idx      = 0;
    d->field_idx    = 0;
    d->in_escape    = 0;
    d->dropping     = 0;
}

frame_err_t frame_decoder_init(frame_decoder_t *d,
                               frame_rx_cb_t rx_cb,
                               frame_err_cb_t err_cb,
                               void *user,
                               uint8_t *payload_buf,
                               size_t payload_buf_size)
{
    if (d == NULL || rx_cb == NULL || payload_buf == NULL) {
        return FRAME_ERR_NULL_ARG;
    }
    if (payload_buf_size > FRAME_MAX_PAYLOAD) {
        return FRAME_ERR_OVERSIZE;
    }

    d->rx_cb            = rx_cb;
    d->err_cb           = err_cb;
    d->user             = user;
    d->payload_buf      = payload_buf;
    d->payload_buf_size = payload_buf_size;
    d->in_frame         = 0;
    decoder_clear_frame_state(d);
    return FRAME_OK;
}

void frame_decoder_reset(frame_decoder_t *d)
{
    if (d == NULL) {
        return;
    }
    d->in_frame = 0;
    decoder_clear_frame_state(d);
}

/*
 * Notify the err_cb (if registered) and mark the current frame as dropped so
 * subsequent bytes are discarded until the next FLAG.
 */
static void decoder_drop(frame_decoder_t *d, frame_err_t err)
{
    if (!d->dropping) {
        d->dropping = 1;
        if (d->err_cb != NULL) {
            d->err_cb(err, d->user);
        }
    }
}

/*
 * Consume a single unstuffed body byte, advancing the field/payload/CRC
 * sub-state machines. Anything past the expected byte count poisons the
 * frame.
 */
static void decoder_consume_byte(frame_decoder_t *d, uint8_t b)
{
    if (d->dropping) {
        return;
    }

    if (d->field_idx < 4) {
        switch (d->field_idx) {
            case 0: d->seq          = b;                                break;
            case 1: d->type         = b;                                break;
            case 2: d->declared_len = b;                                break;
            case 3: d->declared_len |= (uint16_t)((uint16_t)b << 8);    break;
            default: break;
        }
        d->field_idx++;

        if (d->field_idx == 4) {
            if (d->declared_len > FRAME_MAX_PAYLOAD ||
                d->declared_len > d->payload_buf_size) {
                decoder_drop(d, FRAME_ERR_OVERSIZE);
                return;
            }
            if ((unsigned)d->type > (unsigned)FRAME_TYPE__MAX) {
                /*
                 * Unknown type with a CRC-valid frame still gets up-called as
                 * the decoder is intentionally type-tolerant; it's the upper
                 * layer's job to reject. We don't drop here.
                 */
            }
        }
        return;
    }

    if (d->payload_idx < d->declared_len) {
        d->payload_buf[d->payload_idx++] = b;
        return;
    }

    if (d->crc_idx < 2) {
        d->crc_bytes[d->crc_idx++] = b;
        return;
    }

    /* Body too long — caller sent more bytes than LEN advertised. */
    decoder_drop(d, FRAME_ERR_TRUNC);
}

/*
 * Closing FLAG handler: validate the assembled frame, fire callbacks,
 * and reset for the next frame. The caller must set in_frame=1 again
 * afterwards because the closing FLAG is also the opening FLAG of the next
 * frame in HDLC.
 */
static void decoder_finalize(frame_decoder_t *d)
{
    /* Empty FLAG-FLAG pair (no body bytes seen) is a benign idle marker. */
    if (!d->dropping && !d->in_escape && d->field_idx == 0) {
        return;
    }

    if (d->dropping) {
        /* Error was already reported via err_cb when we entered drop. */
        return;
    }

    if (d->in_escape) {
        if (d->err_cb != NULL) {
            d->err_cb(FRAME_ERR_TRUNC, d->user);
        }
        return;
    }

    /*
     * Frame must have all four header bytes, the full payload, and both CRC
     * bytes consumed before the closing FLAG.
     */
    if (d->field_idx != 4 ||
        d->payload_idx != d->declared_len ||
        d->crc_idx != 2) {
        if (d->err_cb != NULL) {
            d->err_cb(FRAME_ERR_TRUNC, d->user);
        }
        return;
    }

    /* Recompute CRC over [seq, type, len_lo, len_hi, payload]. */
    const uint8_t header[4] = {
        d->seq,
        d->type,
        (uint8_t)(d->declared_len & 0xFFu),
        (uint8_t)((d->declared_len >> 8) & 0xFFu),
    };
    uint16_t crc = frame_crc16(header, sizeof(header));
    for (uint16_t i = 0; i < d->payload_idx; ++i) {
        crc ^= ((uint16_t)d->payload_buf[i]) << 8;
        for (int b = 0; b < 8; ++b) {
            if (crc & 0x8000u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }

    uint16_t recv_crc =
        (uint16_t)d->crc_bytes[0] |
        (uint16_t)((uint16_t)d->crc_bytes[1] << 8);

    if (crc != recv_crc) {
        if (d->err_cb != NULL) {
            d->err_cb(FRAME_ERR_CRC, d->user);
        }
        return;
    }

    if ((unsigned)d->type > (unsigned)FRAME_TYPE__MAX) {
        if (d->err_cb != NULL) {
            d->err_cb(FRAME_ERR_BAD_TYPE, d->user);
        }
        return;
    }

    d->rx_cb(d->seq, (frame_type_t)d->type, d->payload_buf, d->payload_idx,
             d->user);
}

void frame_decoder_feed(frame_decoder_t *d, const uint8_t *bytes, size_t n)
{
    if (d == NULL || bytes == NULL) {
        return;
    }

    for (size_t i = 0; i < n; ++i) {
        uint8_t b = bytes[i];

        if (b == FRAME_FLAG) {
            if (d->in_frame) {
                decoder_finalize(d);
            }
            decoder_clear_frame_state(d);
            d->in_frame = 1;
            continue;
        }

        if (!d->in_frame) {
            /* Garbage between frames is silently dropped. */
            continue;
        }

        if (b == FRAME_ESC) {
            if (d->in_escape) {
                /* Two ESCs in a row — malformed stuffing. */
                decoder_drop(d, FRAME_ERR_TRUNC);
                d->in_escape = 0;
                continue;
            }
            d->in_escape = 1;
            continue;
        }

        if (d->in_escape) {
            d->in_escape = 0;
            b ^= FRAME_ESC_XOR;
        }

        decoder_consume_byte(d, b);
    }
}

/* ------------------------------------------------------------------------
 * Reliable layer (sliding window of 1)
 * ------------------------------------------------------------------------ */

frame_err_t frame_link_init(frame_link_t *link,
                            frame_link_write_cb_t write_cb,
                            frame_link_now_ms_cb_t now_ms_cb,
                            void *user,
                            uint32_t timeout_ms,
                            uint8_t max_retries)
{
    if (link == NULL || write_cb == NULL || now_ms_cb == NULL) {
        return FRAME_ERR_NULL_ARG;
    }
    memset(link, 0, sizeof(*link));
    link->write       = write_cb;
    link->now_ms      = now_ms_cb;
    link->user        = user;
    link->timeout_ms  = timeout_ms;
    link->max_retries = max_retries;
    link->state       = FRAME_LINK_IDLE;
    link->tx_seq      = 0;
    return FRAME_OK;
}

frame_err_t frame_link_send(frame_link_t *link,
                            frame_type_t type,
                            const uint8_t *payload, size_t payload_len)
{
    if (link == NULL) {
        return FRAME_ERR_NULL_ARG;
    }
    if (link->state != FRAME_LINK_IDLE) {
        /* Sliding window of 1: reject overlapping sends. */
        return FRAME_ERR_BUF_SMALL;
    }
    if (payload_len > FRAME_MAX_PAYLOAD) {
        return FRAME_ERR_OVERSIZE;
    }

    int n = frame_encode(link->tx_seq, type,
                         payload, payload_len,
                         link->encoded, sizeof(link->encoded));
    if (n < 0) {
        return (frame_err_t)n;
    }

    /* Stash the unencoded payload so retransmits can reuse the buffer. */
    if (payload_len > 0 && payload != NULL) {
        memcpy(link->pending_payload, payload, payload_len);
    }
    link->pending_type = (uint8_t)type;
    link->pending_len  = (uint16_t)payload_len;
    link->encoded_len  = (size_t)n;

    if (link->write(link->encoded, link->encoded_len, link->user) != 0) {
        /*
         * Transport failure on first send: leave state IDLE so the caller can
         * retry on its own terms. Do not advance tx_seq.
         */
        return FRAME_ERR_BUF_SMALL;
    }

    link->state         = FRAME_LINK_AWAITING_ACK;
    link->tx_started_ms = link->now_ms(link->user);
    link->attempts      = 1;
    return FRAME_OK;
}

int frame_link_on_ack(frame_link_t *link, uint8_t seq)
{
    if (link == NULL || link->state != FRAME_LINK_AWAITING_ACK) {
        return 0;
    }
    if (seq != link->tx_seq) {
        return 0;
    }
    link->state  = FRAME_LINK_IDLE;
    link->tx_seq = (uint8_t)(link->tx_seq + 1u);
    return 1;
}

/*
 * Perform a retransmit of the pending frame. Returns 1 on success, -1 if no
 * attempts remain (caller should treat the send as failed and reset).
 */
static int link_retransmit(frame_link_t *link)
{
    if (link->attempts > link->max_retries) {
        link->state = FRAME_LINK_IDLE;
        return -1;
    }
    if (link->write(link->encoded, link->encoded_len, link->user) != 0) {
        /*
         * Treat a transport-write error like an attempted retransmit: count
         * it against the budget so a stuck transport doesn't loop forever.
         */
        link->attempts++;
        link->tx_started_ms = link->now_ms(link->user);
        return 1;
    }
    link->attempts++;
    link->tx_started_ms = link->now_ms(link->user);
    return 1;
}

int frame_link_on_nack(frame_link_t *link, uint8_t seq)
{
    (void)seq;
    if (link == NULL || link->state != FRAME_LINK_AWAITING_ACK) {
        return 0;
    }
    /*
     * In a sliding-window-of-1 link there is exactly one frame in flight at
     * any time, so any NACK received while AWAITING_ACK refers to that frame.
     * The seq carried in the NACK is the receiver's "last good" — i.e.
     * informational — and intentionally ignored here.
     */
    return link_retransmit(link) > 0 ? 1 : 0;
}

int frame_link_tick(frame_link_t *link)
{
    if (link == NULL || link->state != FRAME_LINK_AWAITING_ACK) {
        return 0;
    }
    uint32_t now = link->now_ms(link->user);
    /*
     * Use unsigned subtraction so wraparound past 2^32 still produces a
     * monotonic delta as long as the timeout fits in 31 bits.
     */
    if ((uint32_t)(now - link->tx_started_ms) < link->timeout_ms) {
        return 0;
    }
    return link_retransmit(link);
}

int frame_link_on_rx(frame_link_t *link, uint8_t seq)
{
    if (link == NULL) {
        return 0;
    }
    if (link->rx_have_last && link->rx_last_seq == seq) {
        return 0;
    }
    link->rx_have_last = 1;
    link->rx_last_seq  = seq;
    return 1;
}
