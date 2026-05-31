#!/usr/bin/env python3
"""Pretty-print slot metadata + image headers from a flashed STM32F411 board.

Phase 1.7 / Plan 001 debugging tool.  Reads the four interesting regions
of on-chip flash (slot A metadata, slot B metadata, slot A header, slot B
header) and decodes them against the format spec in tools/_img_format.py.

Two read backends:

  --backend openocd   Drive openocd `mdb`/`mdw` to dump bytes via ST-LINK.
                      Default; matches scripts/run_hil_tests.py.
  --backend file      Read from a local 512 KB flash dump file (handy
                      offline; produced via openocd `dump_image`).

Exit codes:
    0  succeeded; output is pretty-printed
    1  parse failed (CRC / magic / version mismatch)
    2  read backend failed (openocd not present, dump file unreadable)

Usage:
  python3 tools/partition_dump.py
  python3 tools/partition_dump.py --hla-serial 066BFF...
  python3 tools/partition_dump.py --backend file --image build/flash_dump.bin
"""

import argparse
import hashlib
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

# Allow running as a script from anywhere.
sys.path.insert(0, str(Path(__file__).resolve().parent))
import _img_format as fmt  # noqa: E402

# Slot map — single source of truth is lib/flash/inc/flash_slot.h on the
# C side.  Keep these in sync; the round-trip test in tests/tools/ would
# catch a drift only if we wrote a sign step that targeted slot B.
SLOT_A_BASE     = 0x08010000
SLOT_B_BASE     = 0x08040000
SLOT_A_METADATA = 0x08004000
SLOT_B_METADATA = 0x08008000
FLASH_BASE      = 0x08000000
FLASH_LEN       = 512 * 1024  # 0x80000


def read_openocd(addr: int, length: int, hla_serial: str) -> bytes:
    """Dump `length` bytes starting at `addr` via openocd to a tmpfile."""
    tmp = Path(tempfile.mkstemp(prefix="pd_", suffix=".bin")[1])
    cmd = ["openocd"]
    if hla_serial:
        cmd += ["-c", f"hla_serial {hla_serial}"]
    cmd += [
        "-f", "board/st_nucleo_f4.cfg",
        "-c", "init",
        "-c", "reset halt",
        "-c", f"dump_image {tmp} {addr:#010x} {length}",
        "-c", "exit",
    ]
    try:
        subprocess.run(cmd, check=True, capture_output=True, timeout=30)
        return tmp.read_bytes()
    finally:
        try:
            tmp.unlink()
        except OSError:
            pass


def read_file(addr: int, length: int, image: Path) -> bytes:
    """Read `length` bytes from a 512 KB flash dump."""
    blob = image.read_bytes()
    if len(blob) < FLASH_LEN:
        raise RuntimeError(f"image {image} is {len(blob)} bytes, expected >= {FLASH_LEN}")
    off = addr - FLASH_BASE
    if off < 0 or off + length > len(blob):
        raise RuntimeError(f"address {addr:#010x} not covered by image of {len(blob)} bytes")
    return blob[off : off + length]


def parse_metadata(buf: bytes) -> dict | None:
    """Parse 36-byte slot-metadata buffer.  Returns None on CRC/magic mismatch."""
    if len(buf) < fmt.IMG_SLOT_METADATA_SIZE:
        return None
    pre = buf[: fmt.IMG_SLOT_METADATA_SIZE - 4]
    crc_stored = struct.unpack_from("<I", buf, fmt.IMG_SLOT_METADATA_SIZE - 4)[0]
    if fmt.crc32(pre) != crc_stored:
        return {"valid": False, "raw": buf, "reason": "CRC mismatch"}
    fields = struct.unpack(fmt._SLOT_PRECRC_FMT, pre)
    magic, ver, active, fail, monotonic, *_ = fields
    if magic != fmt.IMG_SLOT_METADATA_MAGIC:
        return {"valid": False, "raw": buf, "reason": f"bad magic {magic:#010x}"}
    if ver != fmt.IMG_SLOT_METADATA_VERSION:
        return {"valid": False, "raw": buf, "reason": f"bad version {ver}"}
    return {
        "valid": True,
        "magic": magic, "version": ver,
        "active": active, "fail_count": fail,
        "monotonic_counter": monotonic,
        "crc": crc_stored,
    }


def parse_header(buf: bytes) -> dict | None:
    if len(buf) < fmt.IMG_HEADER_SIZE:
        return None
    if buf == b"\xff" * len(buf):
        return {"valid": False, "reason": "erased (all 0xFF)"}
    pre = buf[: fmt.IMG_HEADER_SIZE - 4]
    crc_stored = struct.unpack_from("<I", buf, fmt.IMG_HEADER_SIZE - 4)[0]
    if fmt.crc32(pre) != crc_stored:
        return {"valid": False, "reason": f"CRC mismatch (stored={crc_stored:#010x})"}
    fields = struct.unpack(fmt._HEADER_PRECRC_FMT, pre)
    magic, hver, iver, itype, psize, poff, sha, sig, *_ = fields
    if magic != fmt.IMG_HEADER_MAGIC:
        return {"valid": False, "reason": f"bad magic {magic:#010x}"}
    return {
        "valid": True,
        "magic": magic, "header_version": hver, "image_version": iver,
        "image_type": itype, "payload_size": psize, "payload_offset": poff,
        "sha256": sha, "signature": sig, "crc": crc_stored,
    }


def fmt_bytes_short(b: bytes, n: int = 8) -> str:
    head = b[:n].hex()
    suffix = "..." if len(b) > n else ""
    return f"{head}{suffix}"


def render_metadata(name: str, addr: int, info: dict | None) -> None:
    print(f"  Slot {name} metadata @ {addr:#010x}")
    if info is None:
        print("    (read failed)")
        return
    if not info.get("valid"):
        print(f"    INVALID: {info.get('reason', 'unknown')}")
        return
    print(f"    magic            : {info['magic']:#010x} ('SLOT')")
    print(f"    metadata_version : {info['version']}")
    print(f"    active           : {info['active']}")
    print(f"    fail_count       : {info['fail_count']}")
    print(f"    monotonic_counter: {info['monotonic_counter']}")
    print(f"    crc              : {info['crc']:#010x}")


def render_header(name: str, addr: int, info: dict | None,
                  payload: bytes | None = None) -> None:
    print(f"  Slot {name} image header @ {addr:#010x}")
    if info is None or not info.get("valid"):
        reason = info.get("reason", "read failed") if info else "read failed"
        print(f"    INVALID: {reason}")
        return
    type_name = {1: "BOOTLOADER", 2: "APP"}.get(info["image_type"],
                                                f"UNKNOWN({info['image_type']})")
    print(f"    magic            : {info['magic']:#010x} ('IMGH')")
    print(f"    header_version   : {info['header_version']}")
    print(f"    image_version    : {info['image_version']}")
    print(f"    image_type       : {type_name}")
    print(f"    payload_size     : {info['payload_size']}")
    print(f"    payload_offset   : {info['payload_offset']}")
    print(f"    sha256           : {fmt_bytes_short(info['sha256'], 16)}")
    print(f"    signature        : {fmt_bytes_short(info['signature'], 16)}")
    print(f"    crc              : {info['crc']:#010x}")
    if payload is not None:
        actual = hashlib.sha256(payload).hexdigest()
        match = "MATCH" if bytes.fromhex(actual) == info["sha256"] else "MISMATCH"
        print(f"    payload sha256   : {actual[:32]}... ({match})")


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--backend", choices=["openocd", "file"], default="openocd")
    p.add_argument("--hla-serial", default="",
                   help="ST-LINK serial when --backend=openocd. Optional if "
                        "only one probe is connected.")
    p.add_argument("--image", type=Path,
                   help="Flash dump file when --backend=file (must be >= 512 KB).")
    p.add_argument("--no-payload-check", action="store_true",
                   help="Skip recomputing payload SHA-256 (faster on big slots).")
    args = p.parse_args(argv)

    if args.backend == "file" and args.image is None:
        p.error("--backend=file requires --image")

    def reader(addr: int, length: int) -> bytes:
        if args.backend == "openocd":
            return read_openocd(addr, length, args.hla_serial)
        return read_file(addr, length, args.image)

    try:
        md_a_buf = reader(SLOT_A_METADATA, fmt.IMG_SLOT_METADATA_SIZE)
        md_b_buf = reader(SLOT_B_METADATA, fmt.IMG_SLOT_METADATA_SIZE)
    except Exception as e:
        print(f"ERROR: metadata read failed: {e}", file=sys.stderr)
        return 2

    print("=== Slot metadata ===")
    render_metadata("A", SLOT_A_METADATA, parse_metadata(md_a_buf))
    render_metadata("B", SLOT_B_METADATA, parse_metadata(md_b_buf))

    # Read just the header for each slot to get payload_size, then read
    # the payload only if we want the SHA cross-check.
    print("\n=== Slot images ===")
    for name, base in (("A", SLOT_A_BASE), ("B", SLOT_B_BASE)):
        try:
            hdr_buf = reader(base, fmt.IMG_HEADER_SIZE)
        except Exception as e:
            print(f"  Slot {name} image header @ {base:#010x}")
            print(f"    READ FAILED: {e}")
            continue
        info = parse_header(hdr_buf)
        payload = None
        if (info and info.get("valid") and not args.no_payload_check
                and info["payload_size"] <= 256 * 1024):
            try:
                payload = reader(base + info["payload_offset"], info["payload_size"])
            except Exception as e:
                print(f"  (could not read payload for SHA check: {e})")
        render_header(name, base, info, payload)

    return 0


if __name__ == "__main__":
    sys.exit(main())
