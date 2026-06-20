#!/usr/bin/env python3
"""Stream a signed firmware image to the device's bootloader OTA receiver.

Plan 001 Phase 1.8 host driver.  Speaks the framing protocol implemented by
lib/framing/ to the bootloader OTA receiver (apps/bootloader/loader/ota.c).

Flow:
  1. Open the serial port at the requested baud (default 115200).
  2. Send PING; expect PONG. Confirms the chip is in OTA mode.
  3. Send OTA_BEGIN { slot, total_size }; expect ACK.
  4. Stream OTA_CHUNK { offset, data[N] } frames; expect ACK after each.
  5. Send OTA_END; expect STATUS.
  6. Print STATUS, return non-zero exit code on failure.

Usage:
  python3 tools/ota_send.py \\
      --port /dev/ttyACM0 \\
      --image build/apps/cli/cli_simple/cli_simple.signed.bin \\
      --slot B \\
      --baud 115200

The chip must already be in OTA mode before this tool runs — issue the
`ota_request` CLI command (or write RTC_BKP_DR0 = 0x4F544131 some other
way) and reset the chip.

Exit codes:
    0  OTA succeeded; bootloader reset into the new image.
    1  invalid arguments / usage.
    2  serial open / I/O failure.
    3  protocol failure (timeout, NACK loop, mismatched response).
    4  bootloader reported STATUS != ok.

Dependencies: pyserial (`pip install pyserial`).
"""

from __future__ import annotations

import argparse
import struct
import sys
import time
from pathlib import Path

# Allow running as both a script (python3 tools/ota_send.py) and as a module.
sys.path.insert(0, str(Path(__file__).resolve().parent))
import _framing as fr  # noqa: E402

DEFAULT_BAUD = 115200
DEFAULT_CHUNK_SIZE = 256
ACK_TIMEOUT_S = 1.5
END_TIMEOUT_S = 5.0   # OTA_END runs verify (~150 ms), erase + commit metadata
PING_TIMEOUT_S = 2.0
MAX_RETRIES = 3

SLOT_A = 0
SLOT_B = 1


# ---------------------------------------------------------------------------
# Serial transaction helpers
# ---------------------------------------------------------------------------


def _read_until_frame(ser, decoder: fr.Decoder, timeout_s: float):
    """Block until decoder emits at least one frame or `timeout_s` elapses.

    Returns the first frame, or None on timeout.
    """
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        # pyserial Serial.read returns up to size bytes, blocking until at
        # least one is available or its own timeout expires. We re-block in
        # the loop until our deadline runs out so a slow byte trickle still
        # eventually surfaces a frame.
        chunk = ser.read(256)
        if not chunk:
            continue
        frames = decoder.feed(chunk)
        if frames:
            return frames[0]
    return None


def _send_and_await(
    ser, decoder: fr.Decoder,
    seq: int, type_: int, payload: bytes,
    *, expect_types: list[int], timeout_s: float, retries: int = MAX_RETRIES,
):
    """Encode, send, and await a frame whose type is in `expect_types`.

    Retries up to `retries` times on either a missing reply or a NACK.
    Returns the matched DecodedFrame, or raises RuntimeError on exhaustion.
    """
    encoded = fr.encode_frame(seq, type_, payload)
    last_seen = "no reply"
    for attempt in range(retries + 1):
        ser.write(encoded)
        ser.flush()
        frame = _read_until_frame(ser, decoder, timeout_s)
        if frame is None:
            last_seen = "timeout"
            continue
        if frame.type == fr.TYPE_NACK:
            last_seen = "NACK"
            time.sleep(0.05)
            continue
        if frame.type in expect_types and frame.seq == seq:
            return frame
        # Otherwise: unexpected type or seq mismatch; treat as protocol-level
        # noise and retry.
        last_seen = (
            f"unexpected {fr.TYPE_NAMES.get(frame.type, frame.type)} seq={frame.seq}"
        )
    raise RuntimeError(
        f"OTA exchange failed after {retries + 1} attempts: {last_seen}"
    )


# ---------------------------------------------------------------------------
# OTA stages
# ---------------------------------------------------------------------------


def stage_ping(ser, decoder: fr.Decoder) -> None:
    print("[ping] confirming bootloader OTA mode...")
    frame = _send_and_await(
        ser, decoder, seq=0, type_=fr.TYPE_PING, payload=b"",
        expect_types=[fr.TYPE_PONG],
        timeout_s=PING_TIMEOUT_S,
    )
    print(f"[ping] PONG seq={frame.seq}")


def stage_begin(ser, decoder: fr.Decoder, slot: int, total_size: int) -> int:
    payload = struct.pack("<BI", slot, total_size)
    print(f"[begin] slot={'A' if slot == SLOT_A else 'B'} size={total_size} bytes")
    seq = 1
    _send_and_await(
        ser, decoder, seq=seq, type_=fr.TYPE_OTA_BEGIN, payload=payload,
        expect_types=[fr.TYPE_ACK],
        timeout_s=END_TIMEOUT_S,  # erasing 192 KB takes a moment
    )
    return seq + 1


def stage_chunks(
    ser, decoder: fr.Decoder, image: bytes, start_seq: int, chunk_size: int,
) -> tuple[int, int]:
    """Stream the image in OTA_CHUNK frames. Returns (next_seq, retx_count)."""
    total = len(image)
    sent = 0
    retx = 0
    seq = start_seq
    t0 = time.time()
    while sent < total:
        n = min(chunk_size, total - sent)
        payload = struct.pack("<I", sent) + image[sent : sent + n]
        try:
            _send_and_await(
                ser, decoder, seq=seq, type_=fr.TYPE_OTA_CHUNK,
                payload=payload,
                expect_types=[fr.TYPE_ACK],
                timeout_s=ACK_TIMEOUT_S,
            )
        except RuntimeError:
            raise
        sent += n
        seq = (seq + 1) & 0xFF
        # Cheap progress: print every ~5%.
        pct = (sent * 100) // total
        if pct != ((sent - n) * 100) // total:
            elapsed = time.time() - t0
            kbps = (sent / 1024.0) / elapsed if elapsed > 0 else 0.0
            print(f"[chunk] {sent}/{total} bytes ({pct}%) {kbps:.1f} KiB/s")
    elapsed = time.time() - t0
    kbps = (total / 1024.0) / elapsed if elapsed > 0 else 0.0
    print(f"[chunk] done: {total} bytes in {elapsed:.2f} s ({kbps:.1f} KiB/s)")
    return seq, retx


def stage_end(ser, decoder: fr.Decoder, seq: int) -> int:
    print("[end] verifying...")
    frame = _send_and_await(
        ser, decoder, seq=seq, type_=fr.TYPE_OTA_END, payload=b"",
        expect_types=[fr.TYPE_STATUS],
        timeout_s=END_TIMEOUT_S,
    )
    if not frame.payload:
        raise RuntimeError("STATUS frame had no payload")
    status = frame.payload[0]
    name = fr.STATUS_NAMES.get(status, f"unknown({status})")
    print(f"[end] STATUS={name}")
    return status


# ---------------------------------------------------------------------------
# Main entry
# ---------------------------------------------------------------------------


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(
        description="Stream a signed image into the bootloader OTA receiver."
    )
    p.add_argument("--port", required=True, help="Serial device, e.g. /dev/ttyACM0")
    p.add_argument("--image", required=True,
                   help="Path to a .signed.bin produced by tools/sign_image.py")
    p.add_argument("--slot", required=True, choices=["A", "B"],
                   help="Target slot — must be the inactive slot")
    p.add_argument("--baud", type=int, default=DEFAULT_BAUD,
                   help=f"Serial baud rate (default {DEFAULT_BAUD})")
    p.add_argument("--chunk", type=int, default=DEFAULT_CHUNK_SIZE,
                   help=f"Bytes per OTA_CHUNK (default {DEFAULT_CHUNK_SIZE})")
    args = p.parse_args(argv)

    if args.chunk <= 0 or args.chunk > fr.MAX_PAYLOAD - 4:
        print(f"--chunk must be 1..{fr.MAX_PAYLOAD - 4}", file=sys.stderr)
        return 1

    image_path = Path(args.image)
    if not image_path.exists():
        print(f"image not found: {image_path}", file=sys.stderr)
        return 1
    image = image_path.read_bytes()
    if not image:
        print(f"image is empty: {image_path}", file=sys.stderr)
        return 1

    slot_id = SLOT_A if args.slot == "A" else SLOT_B

    try:
        import serial
    except ImportError:
        print("pyserial not installed. Run: pip3 install pyserial", file=sys.stderr)
        return 2

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.2)
    except Exception as e:
        print(f"could not open {args.port}: {e}", file=sys.stderr)
        return 2

    decoder = fr.Decoder()
    try:
        # Drain any pending output so we don't decode prior-boot bytes.
        ser.reset_input_buffer()

        try:
            stage_ping(ser, decoder)
            seq = stage_begin(ser, decoder, slot_id, len(image))
            seq, _retx = stage_chunks(ser, decoder, image, seq, args.chunk)
            status = stage_end(ser, decoder, seq)
        except RuntimeError as e:
            print(f"OTA failed: {e}", file=sys.stderr)
            return 3

        if status == fr.STATUS_ROLLBACK_REJECTED:
            return 5
        if status != fr.STATUS_OK:
            return 4
        print("OTA OK — bootloader is resetting into the new image.")
        return 0
    finally:
        try:
            ser.close()
        except Exception:
            pass


if __name__ == "__main__":
    sys.exit(main())
