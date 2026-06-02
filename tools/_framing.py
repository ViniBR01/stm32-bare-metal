"""Pure-Python mirror of lib/framing — host side of Plan 001 Phase 1.8.

Single Python source of truth for the wire format defined in
lib/framing/inc/framing.h. Used by tools/ota_send.py and scripts/run_ota_test.py.

Drift between this module and the C side is a load-bearing bug — but unlike
img_header there is no auto-running CI round-trip test for framing today.
The HIL ota_send.py end-to-end test catches any divergence in practice
because the on-target receiver and this Python encoder must agree at
every byte for the OTA to complete.

Dependencies: stdlib only.
"""

from __future__ import annotations

from dataclasses import dataclass

# Wire-format constants.
FLAG = 0x7E
ESC = 0x7D
ESC_XOR = 0x20

MAX_PAYLOAD = 1024

# Frame types — must match `frame_type_t` in lib/framing/inc/framing.h.
TYPE_DATA = 0
TYPE_ACK = 1
TYPE_NACK = 2
TYPE_OTA_BEGIN = 3
TYPE_OTA_CHUNK = 4
TYPE_OTA_END = 5
TYPE_PING = 6
TYPE_PONG = 7
TYPE_STATUS = 8

TYPE_NAMES = {
    TYPE_DATA: "DATA",
    TYPE_ACK: "ACK",
    TYPE_NACK: "NACK",
    TYPE_OTA_BEGIN: "OTA_BEGIN",
    TYPE_OTA_CHUNK: "OTA_CHUNK",
    TYPE_OTA_END: "OTA_END",
    TYPE_PING: "PING",
    TYPE_PONG: "PONG",
    TYPE_STATUS: "STATUS",
}

# OTA STATUS payload byte values — must match `ota_status_t` in
# apps/bootloader/loader/ota.h.
STATUS_OK = 0
STATUS_VERIFY_FAILED = 1
STATUS_WRITE_FAILED = 2
STATUS_PROTOCOL_ERROR = 3

STATUS_NAMES = {
    STATUS_OK: "ok",
    STATUS_VERIFY_FAILED: "verify_failed",
    STATUS_WRITE_FAILED: "write_failed",
    STATUS_PROTOCOL_ERROR: "protocol_error",
}


def crc16_ccitt(buf: bytes) -> int:
    """CRC-16-CCITT, poly 0x1021, init 0xFFFF, no final XOR.

    Bytewise loop, mirrors lib/framing/src/framing.c::frame_crc16. Reference
    vector: crc16("123456789") == 0x29B1.
    """
    crc = 0xFFFF
    for b in buf:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def _stuff(byte: int) -> bytes:
    if byte == FLAG or byte == ESC:
        return bytes([ESC, byte ^ ESC_XOR])
    return bytes([byte])


def encode_frame(seq: int, type_: int, payload: bytes = b"") -> bytes:
    """Encode (seq, type, payload) into the on-wire framing format.

    Mirrors `frame_encode` in lib/framing/src/framing.c byte-for-byte:
    body = [seq, type, len_lo, len_hi, payload]; CRC-16-CCITT over body;
    body + CRC bytes are stuffed; full frame is wrapped in FLAG bytes.
    """
    if not (0 <= seq <= 0xFF):
        raise ValueError(f"seq out of range: {seq}")
    if type_ not in TYPE_NAMES:
        raise ValueError(f"unknown frame type: {type_}")
    if len(payload) > MAX_PAYLOAD:
        raise ValueError(f"payload too large: {len(payload)} > {MAX_PAYLOAD}")

    header = bytes(
        [seq, type_, len(payload) & 0xFF, (len(payload) >> 8) & 0xFF]
    )
    crc = crc16_ccitt(header + payload)
    crc_bytes = bytes([crc & 0xFF, (crc >> 8) & 0xFF])

    out = bytearray([FLAG])
    for b in header:
        out += _stuff(b)
    for b in payload:
        out += _stuff(b)
    for b in crc_bytes:
        out += _stuff(b)
    out.append(FLAG)
    return bytes(out)


@dataclass
class DecodedFrame:
    seq: int
    type: int
    payload: bytes


class Decoder:
    """Stateful decoder mirroring `frame_decoder_t`.

    Feed bytes via `feed()`; complete frames are returned via the iterator.
    Behaviour intentionally matches the C side: bad CRC drops the frame
    silently (the Python caller can inspect `crc_errors` or `truncations`),
    and the decoder resyncs on the next FLAG.
    """

    def __init__(self) -> None:
        self._buf = bytearray()
        self._in_frame = False
        self._in_escape = False
        self._dropping = False
        self.crc_errors = 0
        self.truncations = 0
        self.bad_types = 0

    def feed(self, data: bytes) -> list[DecodedFrame]:
        out: list[DecodedFrame] = []
        for b in data:
            if b == FLAG:
                if self._in_frame:
                    frame = self._finalize()
                    if frame is not None:
                        out.append(frame)
                self._buf.clear()
                self._in_frame = True
                self._in_escape = False
                self._dropping = False
                continue
            if not self._in_frame:
                continue
            if b == ESC:
                if self._in_escape:
                    self._dropping = True
                self._in_escape = True
                continue
            if self._in_escape:
                self._in_escape = False
                b ^= ESC_XOR
            self._buf.append(b)
        return out

    def _finalize(self) -> DecodedFrame | None:
        if self._dropping:
            self.truncations += 1
            return None
        if self._in_escape:
            self.truncations += 1
            return None
        if len(self._buf) < 6:
            # Empty FLAG-FLAG idle pair, or truncated frame.
            if len(self._buf) > 0:
                self.truncations += 1
            return None
        seq = self._buf[0]
        type_ = self._buf[1]
        declared_len = self._buf[2] | (self._buf[3] << 8)
        body_end = 4 + declared_len
        if body_end + 2 != len(self._buf):
            self.truncations += 1
            return None
        payload = bytes(self._buf[4:body_end])
        crc_recv = self._buf[body_end] | (self._buf[body_end + 1] << 8)
        crc_calc = crc16_ccitt(bytes(self._buf[:body_end]))
        if crc_recv != crc_calc:
            self.crc_errors += 1
            return None
        if type_ not in TYPE_NAMES:
            self.bad_types += 1
            return None
        return DecodedFrame(seq=seq, type=type_, payload=payload)
