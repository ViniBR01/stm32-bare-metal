#!/usr/bin/env python3
"""Plan 001 Phase 1.7 A/B slot HIL test.

Four passes covering the slot-pick + fallback decision tree implemented
in apps/bootloader/loader/main.c.  All passes flash the bootloader's
metadata sectors via OpenOCD before each scenario so the test does not
depend on whatever metadata happens to be on the board between runs.

  Pass 1  Slot A active (clean), slot B erased
            → boot expects "trying slot A" + "verify ok slot=A"
              + "APP: blinky alive".  No fallback.

  Pass 2  Slot B active (clean), slot A erased.
            → boot expects "trying slot B" + "verify ok slot=B" +
              "APP: blinky alive".  No fallback.

  Pass 3  Slot A corrupt (payload byte flipped), slot B clean,
          slot A active.
            → boot expects "trying slot A" + "verify FAILED" + "falling
              back to slot B" + "verify ok slot=B" + "APP: blinky alive".

  Pass 4  Both slots corrupt.
            → boot expects "both slots failed verify".  APP: blinky alive
              must NOT appear.

Slot map (must match lib/flash/inc/flash_slot.h):
   slot A payload   : 0x08010000
   slot B payload   : 0x08040000
   slot A metadata  : 0x08004000  (sector 1)
   slot B metadata  : 0x08008000  (sector 2)

Exit codes:
    0  all four passes succeeded
    1  one or more passes failed an assertion
    2  build / flash / serial-open error
"""

import argparse
import re
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from xml.etree.ElementTree import Element, SubElement, ElementTree, indent

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_hil_tests as hil  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import _img_format as fmt  # noqa: E402

SLOT_A_BASE      = 0x08010000
SLOT_B_BASE      = 0x08040000
SLOT_A_METADATA  = 0x08004000
SLOT_B_METADATA  = 0x08008000

VERIFY_OK_RE   = re.compile(r"BL:\s+verify\s+ok\s+slot=(\w)")
VERIFY_FAIL_RE = re.compile(r"BL:\s+slot\s+(\w)\s+verify\s+FAILED:")
FALLBACK_RE    = re.compile(r"BL:\s+falling\s+back\s+to\s+slot\s+(\w)")
TRYING_RE      = re.compile(r"BL:\s+trying\s+slot\s+(\w)")
BOTH_FAIL      = "BL: both slots failed verify"
APP_ALIVE      = "APP: blinky alive"


# -------------------- Build helpers --------------------

def build_app_for_slot(project_root: Path, slot: str) -> Path:
    hil.log_info(f"Building app_blinky_signed for slot {slot}...")
    subprocess.run(
        ["make", "EXAMPLE=app_blinky_signed", f"SLOT={slot}"],
        cwd=project_root, check=True, timeout=120,
    )
    suffix = "" if slot == "A" else "_b"
    signed = (project_root / "build" / "apps" / "bootloader"
              / f"app_blinky_signed{suffix}"
              / f"app_blinky_signed{suffix}.signed.bin")
    if not signed.exists():
        raise RuntimeError(f"expected {signed} after build")
    return signed


def make_tampered(signed: Path) -> Path:
    raw = bytearray(signed.read_bytes())
    payload_offset = struct.unpack_from("<I", raw, 20)[0]
    if len(raw) < payload_offset + 8:
        raise RuntimeError(f"signed image {signed} unexpectedly small")
    raw[payload_offset + 4] ^= 0xFF
    tmp = Path(tempfile.mkstemp(prefix="tampered_", suffix=".signed.bin")[1])
    tmp.write_bytes(bytes(raw))
    return tmp


def make_metadata_blob(active: int, fail_count: int, monotonic: int) -> bytes:
    """Pack a valid 36-byte img_slot_metadata_t."""
    return fmt.pack_slot_metadata(
        active=active, fail_count=fail_count, monotonic_counter=monotonic,
    )


def metadata_to_file(blob: bytes) -> Path:
    """Write a metadata blob to a tmp file for openocd `program`."""
    tmp = Path(tempfile.mkstemp(prefix="md_", suffix=".bin")[1])
    tmp.write_bytes(blob)
    return tmp


# -------------------- Flash helpers --------------------

def openocd(hla_serial: str, *commands: str, timeout: int = 30) -> None:
    """Run OpenOCD with -c init/reset halt/<commands>/exit."""
    cmd = ["openocd"]
    if hla_serial:
        cmd += ["-c", f"hla_serial {hla_serial}"]
    cmd += ["-f", "board/st_nucleo_f4.cfg",
            "-c", "init", "-c", "reset halt"]
    for c in commands:
        cmd += ["-c", c]
    cmd += ["-c", "exit"]
    subprocess.run(cmd, check=True, capture_output=True, timeout=timeout)


def erase_metadata_sector(hla_serial: str, slot: str) -> None:
    """Erase the slot-N metadata sector (sector 1 for A, sector 2 for B)."""
    sector = 1 if slot == "A" else 2
    openocd(hla_serial,
            "flash banks",
            f"flash erase_sector 0 {sector} {sector}")


def program_metadata(hla_serial: str, slot: str, blob: bytes) -> None:
    """Erase + program the metadata sector for `slot` with `blob`."""
    addr = SLOT_A_METADATA if slot == "A" else SLOT_B_METADATA
    f = metadata_to_file(blob)
    try:
        openocd(hla_serial,
                f"program {f} {addr:#010x} verify")
    finally:
        try: f.unlink()
        except OSError: pass


def erase_slot_payload(hla_serial: str, slot: str) -> None:
    """Erase every sector backing a slot's payload region."""
    if slot == "A":
        # Sectors 4 (0x10000), 5 (0x20000)
        sectors = [4, 5]
    else:
        # Sector 6 (0x40000)
        sectors = [6]
    cmds = []
    for s in sectors:
        cmds.append(f"flash erase_sector 0 {s} {s}")
    openocd(hla_serial, *cmds)


# -------------------- Serial capture --------------------

def capture_lines(port: str, timeout: int) -> list[str]:
    try:
        import serial
    except ImportError:
        hil.log_error("pyserial not installed. Run: pip3 install pyserial")
        return []

    lines: list[str] = []
    ser = None
    last_err = None
    for _ in range(3):
        try:
            ser = serial.Serial(port, 115200, timeout=2)
            break
        except Exception as e:
            last_err = e
            time.sleep(1.0)
    if ser is None:
        hil.log_error(f"Serial open failed for {port}: {last_err}")
        sys.stdout.flush()
        return []
    try:
        ser.reset_input_buffer()
        time.sleep(0.5)
        deadline = time.time() + timeout
        while time.time() < deadline:
            if ser.in_waiting:
                line = ser.readline().decode("utf-8", errors="ignore").rstrip()
                if line:
                    lines.append(line)
                    print(f"  {line}")
                    if APP_ALIVE in line:
                        # Read a tiny tail to capture anything else queued.
                        time.sleep(0.5)
                        while ser.in_waiting:
                            l2 = ser.readline().decode("utf-8", errors="ignore").rstrip()
                            if l2:
                                lines.append(l2)
                                print(f"  {l2}")
                        break
                    if BOTH_FAIL in line:
                        time.sleep(2.0)
                        while ser.in_waiting:
                            l2 = ser.readline().decode("utf-8", errors="ignore").rstrip()
                            if l2:
                                lines.append(l2)
                                print(f"  {l2}")
                        break
            else:
                time.sleep(0.05)
    finally:
        try: ser.close()
        except Exception: pass
    return lines


def reset_and_capture(hla_serial: str, timeout: int) -> list[str]:
    """Open the serial port first to drain any prior-boot output, THEN
    issue a reset.  Programming the slot already booted the chip once
    (OpenOCD's `program ... reset exit`); without this ordering we'd
    capture a mix of the prior boot and the new one."""
    port = hil.find_serial_port(hla_serial=hla_serial)
    if not port:
        hil.log_error("Serial port not found")
        return []
    try:
        import serial
    except ImportError:
        hil.log_error("pyserial not installed")
        return []

    # Open + drain.
    ser = None
    last_err = None
    for _ in range(3):
        try:
            ser = serial.Serial(port, 115200, timeout=0.2)
            break
        except Exception as e:
            last_err = e
            time.sleep(1.0)
    if ser is None:
        hil.log_error(f"Serial open failed: {last_err}")
        return []
    try:
        ser.reset_input_buffer()
        # Drain any in-flight bytes from the previous post-flash boot.
        deadline = time.time() + 1.5
        while time.time() < deadline:
            if ser.in_waiting:
                ser.read(ser.in_waiting)
                deadline = time.time() + 0.3
            else:
                time.sleep(0.05)
        ser.reset_input_buffer()
    finally:
        try: ser.close()
        except Exception: pass

    # Now reset and recapture.
    openocd(hla_serial, "reset run")
    time.sleep(0.5)
    return capture_lines(port, timeout)


# -------------------- Pass-level setup helpers --------------------

def setup_clean_slot(hla_serial: str, slot: str, signed: Path) -> None:
    base = SLOT_A_BASE if slot == "A" else SLOT_B_BASE
    hil.log_info(f"Programming clean slot {slot}...")
    if not hil.flash_firmware(signed, hla_serial=hla_serial, slot_base=base):
        raise RuntimeError(f"flash slot {slot} failed")


def setup_corrupt_slot(hla_serial: str, slot: str, signed: Path) -> None:
    tampered = make_tampered(signed)
    try:
        base = SLOT_A_BASE if slot == "A" else SLOT_B_BASE
        hil.log_info(f"Programming tampered slot {slot}...")
        if not hil.flash_firmware(tampered, hla_serial=hla_serial, slot_base=base):
            raise RuntimeError(f"flash tampered slot {slot} failed")
    finally:
        try: tampered.unlink()
        except OSError: pass


def setup_erased_slot(hla_serial: str, slot: str) -> None:
    hil.log_info(f"Erasing slot {slot} payload sectors...")
    erase_slot_payload(hla_serial, slot)


# -------------------- Assertions --------------------

def assert_pass1_a_active(lines: list[str]) -> tuple[bool, list[str]]:
    fails = []
    if not any(TRYING_RE.search(s) and TRYING_RE.search(s).group(1) == "A"
               for s in lines):
        fails.append("missing 'BL: trying slot A'")
    if not any(VERIFY_OK_RE.search(s) and VERIFY_OK_RE.search(s).group(1) == "A"
               for s in lines):
        fails.append("missing 'BL: verify ok slot=A'")
    if not any(APP_ALIVE in s for s in lines):
        fails.append(f"missing {APP_ALIVE!r}")
    if any(FALLBACK_RE.search(s) for s in lines):
        fails.append("unexpected fallback line on a clean A boot")
    return (not fails, fails)


def assert_pass2_b_active(lines: list[str]) -> tuple[bool, list[str]]:
    fails = []
    if not any(TRYING_RE.search(s) and TRYING_RE.search(s).group(1) == "B"
               for s in lines):
        fails.append("missing 'BL: trying slot B'")
    if not any(VERIFY_OK_RE.search(s) and VERIFY_OK_RE.search(s).group(1) == "B"
               for s in lines):
        fails.append("missing 'BL: verify ok slot=B'")
    if not any(APP_ALIVE in s for s in lines):
        fails.append(f"missing {APP_ALIVE!r}")
    if any(FALLBACK_RE.search(s) for s in lines):
        fails.append("unexpected fallback line on a clean B boot")
    return (not fails, fails)


def assert_pass3_a_bad_b_clean(lines: list[str]) -> tuple[bool, list[str]]:
    fails = []
    if not any(TRYING_RE.search(s) and TRYING_RE.search(s).group(1) == "A"
               for s in lines):
        fails.append("missing 'BL: trying slot A' (expected to try active first)")
    if not any(VERIFY_FAIL_RE.search(s) and VERIFY_FAIL_RE.search(s).group(1) == "A"
               for s in lines):
        fails.append("missing 'BL: slot A verify FAILED:'")
    if not any(FALLBACK_RE.search(s) and FALLBACK_RE.search(s).group(1) == "B"
               for s in lines):
        fails.append("missing 'BL: falling back to slot B'")
    if not any(VERIFY_OK_RE.search(s) and VERIFY_OK_RE.search(s).group(1) == "B"
               for s in lines):
        fails.append("missing 'BL: verify ok slot=B' after fallback")
    if not any(APP_ALIVE in s for s in lines):
        fails.append(f"missing {APP_ALIVE!r}")
    return (not fails, fails)


def assert_pass4_both_bad(lines: list[str]) -> tuple[bool, list[str]]:
    fails = []
    if not any(BOTH_FAIL in s for s in lines):
        fails.append(f"missing {BOTH_FAIL!r}")
    if any(APP_ALIVE in s for s in lines):
        fails.append(f"app started despite both slots bad — saw {APP_ALIVE!r}")
    return (not fails, fails)


# -------------------- JUnit XML --------------------

def write_junit_xml(path: str, results: list[tuple[str, bool, list[str]]]) -> None:
    suites = Element("testsuites")
    failures_total = sum(0 if ok else 1 for (_, ok, _) in results)
    suite = SubElement(suites, "testsuite",
                       name="A/B slot fallback (Phase 1.7)",
                       tests=str(len(results)),
                       failures=str(failures_total),
                       errors="0", time="0")
    for name, ok, fails in results:
        tc = SubElement(suite, "testcase", classname="ab_slot_test",
                        name=name, time="0")
        if not ok:
            SubElement(tc, "failure", message="; ".join(fails) or "failed")
    indent(suites)
    ElementTree(suites).write(path, encoding="unicode", xml_declaration=True)


# -------------------- Main --------------------

def main(argv: list[str] | None = None) -> int:
    try:
        sys.stdout.reconfigure(line_buffering=True)
    except Exception:
        pass

    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--board", choices=list(hil.BOARD_REGISTRY.keys()))
    p.add_argument("--hla-serial", default=hil.DEFAULT_HLA_SERIAL)
    p.add_argument("--timeout", type=int, default=20,
                   help="Per-pass serial-capture timeout (seconds).")
    p.add_argument("--junit-xml", default="ab-slot-results.xml")
    args = p.parse_args(argv)

    project_root = hil.get_project_root()
    hla = hil.BOARD_REGISTRY[args.board] if args.board else args.hla_serial

    try:
        signed_a = build_app_for_slot(project_root, "A")
        signed_b = build_app_for_slot(project_root, "B")
    except Exception as e:
        hil.log_error(f"Build failed: {e}")
        write_junit_xml(args.junit_xml,
                        [("build", False, [f"build failed: {e}"])])
        return 2

    md_a_active = make_metadata_blob(active=1, fail_count=0, monotonic=1)
    md_b_active = make_metadata_blob(active=1, fail_count=0, monotonic=1)

    results: list[tuple[str, bool, list[str]]] = []

    # ---------- Pass 1: A active, B erased ----------
    hil.log_info("=== Pass 1: clean slot A active, slot B erased ===")
    try:
        setup_clean_slot(hla, "A", signed_a)
        setup_erased_slot(hla, "B")
        program_metadata(hla, "A", md_a_active)
        erase_metadata_sector(hla, "B")
    except Exception as e:
        hil.log_error(f"Pass 1 setup failed: {e}")
        write_junit_xml(args.junit_xml,
                        [("pass1_setup", False, [str(e)])])
        return 2
    lines = reset_and_capture(hla, args.timeout)
    ok, fails = assert_pass1_a_active(lines)
    results.append(("clean_A_active_boots_A", ok, fails))

    # ---------- Pass 2: B active, A erased ----------
    hil.log_info("=== Pass 2: clean slot B active, slot A erased ===")
    try:
        setup_clean_slot(hla, "B", signed_b)
        setup_erased_slot(hla, "A")
        program_metadata(hla, "B", md_b_active)
        erase_metadata_sector(hla, "A")
    except Exception as e:
        hil.log_error(f"Pass 2 setup failed: {e}")
        write_junit_xml(args.junit_xml, results +
                        [("pass2_setup", False, [str(e)])])
        return 2
    lines = reset_and_capture(hla, args.timeout)
    ok, fails = assert_pass2_b_active(lines)
    results.append(("clean_B_active_boots_B", ok, fails))

    # ---------- Pass 3: A corrupt + active, B clean ----------
    hil.log_info("=== Pass 3: A corrupt (active), B clean → fallback to B ===")
    try:
        setup_corrupt_slot(hla, "A", signed_a)
        setup_clean_slot(hla, "B", signed_b)
        program_metadata(hla, "A", md_a_active)
        erase_metadata_sector(hla, "B")
    except Exception as e:
        hil.log_error(f"Pass 3 setup failed: {e}")
        write_junit_xml(args.junit_xml, results +
                        [("pass3_setup", False, [str(e)])])
        return 2
    lines = reset_and_capture(hla, args.timeout)
    ok, fails = assert_pass3_a_bad_b_clean(lines)
    results.append(("corrupt_A_active_falls_back_to_B", ok, fails))

    # ---------- Pass 4: both corrupt ----------
    hil.log_info("=== Pass 4: both slots corrupt → halt ===")
    try:
        setup_corrupt_slot(hla, "A", signed_a)
        setup_corrupt_slot(hla, "B", signed_b)
        program_metadata(hla, "A", md_a_active)
        erase_metadata_sector(hla, "B")
    except Exception as e:
        hil.log_error(f"Pass 4 setup failed: {e}")
        write_junit_xml(args.junit_xml, results +
                        [("pass4_setup", False, [str(e)])])
        return 2
    lines = reset_and_capture(hla, args.timeout)
    ok, fails = assert_pass4_both_bad(lines)
    results.append(("both_slots_bad_halt", ok, fails))

    # ---------- Restore a sane state for following CI steps ----------
    hil.log_info("=== Restoring clean slot A image ===")
    try:
        setup_clean_slot(hla, "A", signed_a)
        setup_erased_slot(hla, "B")
        erase_metadata_sector(hla, "A")
        erase_metadata_sector(hla, "B")
    except Exception as e:
        hil.log_warning(f"Restore step had a problem (non-fatal): {e}")

    write_junit_xml(args.junit_xml, results)

    all_ok = all(ok for (_, ok, _) in results)
    if all_ok:
        hil.log_success("A/B slot test passed (4/4)")
        return 0
    for name, ok, fails in results:
        if not ok:
            hil.log_error(f"  {name} failed: {'; '.join(fails)}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
