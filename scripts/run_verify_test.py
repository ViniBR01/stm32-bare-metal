#!/usr/bin/env python3
"""Plan 001 Phase 1.6 verify-and-jump HIL test.

Two passes against the same signed app:

  Pass A — clean image:
      Flashes app_blinky_signed.signed.bin and asserts that the bootloader
      logs "BL: verify ok in <cycles> cycles (~<ms> ms)" AND that the app
      then runs ("APP: blinky alive").  Captures the cycle count and checks
      it against the < 500 ms hard cap from Plan 001 §1.3 plus an optional
      soft tolerance loaded from tests/baselines/bootloader_verify.json.

  Pass B — payload-tampered image:
      Reads the signed image, flips one byte at a file offset >= the
      header's payload_offset (NOT inside the header — that would only
      trip the CRC path and never reach signature verification), writes
      the tampered image to a tmp path, flashes it, and asserts:
        - bootloader logs "BL: verify FAILED:" within the timeout, and
        - "APP: blinky alive" never appears.

Exit codes:
    0  both passes pass and verify time within hard cap
    1  one or both passes failed an assertion
    2  build / flash / serial error
"""

import argparse
import json
import re
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from xml.etree.ElementTree import Element, SubElement, ElementTree, indent

# Re-use the board registry, flash helper, and serial-port finder from the
# main HIL runner so the two scripts agree on board topology.
sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_hil_tests as hil  # noqa: E402

# tools/_img_format.py knows the on-flash header layout — same module the
# signer and the C parser are validated against.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import _img_format as fmt  # noqa: E402

CYCLES_PER_MS_AT_100_MHZ = 100_000
HARD_CAP_MS = 500
HARD_CAP_CYCLES = HARD_CAP_MS * CYCLES_PER_MS_AT_100_MHZ

"""Log-line patterns.

Phase 1.6 emitted:
    BL: verify ok in <N> cycles (~<M> ms)
    BL: verify FAILED: <reason>

Phase 1.7 added a slot annotation:
    BL: verify ok slot=<X> in <N> cycles (~<M> ms)
    BL: slot <X> verify FAILED: <reason>

The regexes accept either form so this script keeps working if a future
phase tweaks the format again — the cycle count and the FAILED keyword
are the load-bearing parts.
"""
VERIFY_OK_RE = re.compile(
    r"BL:\s+verify\s+ok(?:\s+slot=\w+)?\s+in\s+(\d+)\s+cycles\s+\(~(\d+)\s+ms\)"
)
VERIFY_FAIL_RE = re.compile(r"BL:(?:\s+slot\s+\w+)?\s+verify\s+FAILED:")
APP_ALIVE_LINE = "APP: blinky alive"


def build_smoke_app(project_root: Path) -> Path | None:
    """Build app_blinky_signed and return its .signed.bin path."""
    hil.log_info("Building app_blinky_signed...")
    subprocess.run(
        ["make", "EXAMPLE=app_blinky_signed"],
        cwd=project_root,
        check=True,
        timeout=120,
    )
    signed = (project_root / "build" / "apps" / "bootloader"
              / "app_blinky_signed" / "app_blinky_signed.signed.bin")
    if not signed.exists():
        hil.log_error(f"Expected {signed} after build")
        return None
    return signed


def make_tampered_image(clean: Path) -> Path:
    """Flip one payload byte and return a new tmp .signed.bin path.

    Flips a byte at file offset = max(payload_offset, header_size + 4).
    The signer pads the payload region to a 256-byte multiple, so flipping
    inside the early payload bytes (which are part of the app vector
    table) reliably trips both the SHA-256 and ECDSA paths.  We pick a
    byte at offset payload_offset + 4 (the second word) so the first word
    — the initial MSP — stays well-formed enough that even if a verify
    bug let the bootloader jump, the chip would still execute the wrong
    Reset_Handler instead of UB-ing in a way that fakes a halt.
    """
    raw = bytearray(clean.read_bytes())
    if len(raw) < fmt.IMG_HEADER_SIZE + 8:
        raise RuntimeError(f"signed image {clean} unexpectedly small ({len(raw)} bytes)")

    # img_header_t layout (lib/img/inc/img_header.h, 140 bytes packed):
    #   u32 magic, u32 header_version, u32 image_version, u32 image_type,
    #   u32 payload_size, u32 payload_offset, ...
    # payload_offset is the 6th u32 → byte offset 5*4 = 20.
    payload_offset = struct.unpack_from("<I", raw, 20)[0]
    if payload_offset < fmt.IMG_HEADER_SIZE:
        raise RuntimeError(
            f"payload_offset {payload_offset} < header size — refusing to tamper"
        )
    if payload_offset + 8 > len(raw):
        raise RuntimeError(f"tamper offset would land outside the signed image")

    flip_at = payload_offset + 4
    raw[flip_at] ^= 0xFF
    hil.log_info(f"Tampered byte at file offset {flip_at} (payload region)")

    tmp = Path(tempfile.mkstemp(prefix="tampered_", suffix=".signed.bin")[1])
    tmp.write_bytes(bytes(raw))
    return tmp


def open_serial_port(port: str):
    """Open the serial port with a short retry loop.

    Returns a `serial.Serial` instance or `None` on failure.  A previous
    HIL step (boot smoke test) may have just released ttyACM0 and on the
    Pi runner the kernel sometimes still holds an exclusive lock for
    ~1 s afterwards.  Three short retries cover that without masking a
    real 'port not available' failure.
    """
    try:
        import serial
    except ImportError:
        hil.log_error("pyserial not installed. Run: pip3 install pyserial")
        return None

    last_err = None
    for _ in range(3):
        try:
            return serial.Serial(port, 115200, timeout=2)
        except Exception as e:
            last_err = e
            time.sleep(1.0)
    hil.log_error(f"Serial error: could not open {port}: {last_err}")
    sys.stdout.flush()
    return None


def capture_lines(ser, timeout: int, stop_on_fail: bool = False) -> list[str]:
    """Drain output from an already-open serial port until timeout or early-exit.

    The port must be opened *before* flashing — see issue #164.  Opening
    after `flash_firmware()` returns races the bootloader's ~150 ms boot
    print sequence and intermittently loses the early `BL: verify ok ...`
    line on the Pi runner.
    """
    lines: list[str] = []
    try:
        deadline = time.time() + timeout
        while time.time() < deadline:
            if ser.in_waiting:
                line = ser.readline().decode("utf-8", errors="ignore").rstrip()
                if line:
                    lines.append(line)
                    print(f"  {line}")
                    if stop_on_fail and VERIFY_FAIL_RE.search(line):
                        # Keep reading briefly to confirm the app does NOT come up.
                        time.sleep(2.0)
                        while ser.in_waiting:
                            l2 = ser.readline().decode("utf-8", errors="ignore").rstrip()
                            if l2:
                                lines.append(l2)
                                print(f"  {l2}")
                        break
                    if VERIFY_OK_RE.search(line) and any(APP_ALIVE_LINE in s for s in lines):
                        break
                    if any(APP_ALIVE_LINE in s for s in lines) and any(
                        VERIFY_OK_RE.search(s) for s in lines
                    ):
                        break
            else:
                time.sleep(0.05)
    except Exception as e:
        hil.log_error(f"Serial error: {e}")
    return lines


def parse_verify_cycles(lines: list[str]) -> int | None:
    for line in lines:
        m = VERIFY_OK_RE.search(line)
        if m:
            return int(m.group(1))
    return None


def assert_clean_pass(lines: list[str]) -> tuple[bool, list[str]]:
    failures: list[str] = []
    if not any(VERIFY_OK_RE.search(s) for s in lines):
        failures.append("missing 'BL: verify ok in <N> cycles' line")
    if not any(APP_ALIVE_LINE in s for s in lines):
        failures.append(f"missing {APP_ALIVE_LINE!r} line")
    cycles = parse_verify_cycles(lines)
    if cycles is not None and cycles > HARD_CAP_CYCLES:
        failures.append(
            f"verify time {cycles} cycles ({cycles // CYCLES_PER_MS_AT_100_MHZ} ms) "
            f"> hard cap {HARD_CAP_CYCLES} cycles ({HARD_CAP_MS} ms)"
        )
    return (not failures, failures)


def assert_tampered_pass(lines: list[str]) -> tuple[bool, list[str]]:
    failures: list[str] = []
    if not any(VERIFY_FAIL_RE.search(s) for s in lines):
        failures.append("missing 'BL: ... verify FAILED: <reason>' line")
    if any(APP_ALIVE_LINE in s for s in lines):
        failures.append(
            f"app started despite tampered image — saw {APP_ALIVE_LINE!r}"
        )
    return (not failures, failures)


def maybe_check_baseline(cycles: int, baseline_path: Path) -> str:
    """Soft baseline check; returns a status string for the test report."""
    if not baseline_path.exists():
        return f"no baseline at {baseline_path} (skipped)"
    try:
        with open(baseline_path) as f:
            data = json.load(f)
    except Exception as e:
        return f"baseline load error: {e}"
    entry = data.get("bootloader_verify_p256_app_blinky")
    if not entry or entry.get("cycles") is None:
        return "baseline entry missing / null (skipped)"
    expected = entry["cycles"]
    tol_pct = entry.get("tolerance_percent", 50)
    lo = expected * (100 - tol_pct) / 100.0
    hi = expected * (100 + tol_pct) / 100.0
    if lo <= cycles <= hi:
        return f"within baseline {expected} ±{tol_pct}%"
    return (f"OUT OF BAND: {cycles} not in [{int(lo)}, {int(hi)}] "
            f"(baseline {expected} ±{tol_pct}%)")


def write_junit_error_xml(path: str, message: str) -> None:
    """Emit a JUnit XML with a single errored testcase so the test-reporter
    step always finds a file, even when the test fails before Pass A
    completes (e.g. serial port could not be opened)."""
    suites = Element("testsuites")
    suite = SubElement(suites, "testsuite",
                       name="Verify (Phase 1.6)",
                       tests="1", failures="0", errors="1", time="0")
    tc = SubElement(suite, "testcase",
                    classname="verify_test",
                    name="harness_setup",
                    time="0")
    SubElement(tc, "error", message=message).text = message
    indent(suites)
    ElementTree(suites).write(path, encoding="unicode", xml_declaration=True)


def write_junit_xml(path: str,
                    clean_ok: bool, clean_fails: list[str], cycles: int | None,
                    tamper_ok: bool, tamper_fails: list[str],
                    baseline_status: str) -> None:
    suites = Element("testsuites")
    suite = SubElement(
        suites, "testsuite",
        name="Verify (Phase 1.6)",
        tests="2",
        failures=str((0 if clean_ok else 1) + (0 if tamper_ok else 1)),
        errors="0",
        time="0",
    )

    clean_tc = SubElement(suite, "testcase",
                          classname="verify_test",
                          name="clean_image_boots_and_verifies",
                          time="0")
    sysout_lines = [f"baseline: {baseline_status}"]
    if cycles is not None:
        sysout_lines.append(
            f"verify_cycles={cycles}  (~{cycles // CYCLES_PER_MS_AT_100_MHZ} ms)"
        )
    SubElement(clean_tc, "system-out").text = "\n".join(sysout_lines)
    if not clean_ok:
        SubElement(clean_tc, "failure", message="; ".join(clean_fails))

    tamper_tc = SubElement(suite, "testcase",
                           classname="verify_test",
                           name="tampered_image_rejected",
                           time="0")
    if not tamper_ok:
        SubElement(tamper_tc, "failure", message="; ".join(tamper_fails))

    indent(suites)
    ElementTree(suites).write(path, encoding="unicode", xml_declaration=True)


def run_pass(image_path: Path, hla_serial: str, timeout: int,
             *, stop_on_fail: bool) -> list[str]:
    """Flash one image, capture serial output, return the lines seen.

    The serial port is opened *before* flashing so that the OpenOCD reset
    that kicks the chip into the bootloader feeds the same already-open
    port — every byte from the boot banner onward lands in the kernel's
    input buffer for `ser`.  Opening after the flash (the previous
    behavior) raced the bootloader's ~150 ms boot prints and lost the
    leading `BL: verify ok ...` line ~1/4 of the time on the Pi runner
    (issue #164).
    """
    port = hil.find_serial_port(hla_serial=hla_serial)
    if not port:
        hil.log_error("Serial port not found")
        return []

    ser = open_serial_port(port)
    if ser is None:
        return []

    try:
        ser.reset_input_buffer()
        if not hil.flash_firmware(image_path, hla_serial=hla_serial):
            return []
        # Brief settle so the bootloader has begun emitting before we read.
        # No longer 2 s — we are already attached to the port and will
        # block on `ser.readline()` for up to `timeout` seconds anyway.
        time.sleep(0.2)
        return capture_lines(ser, timeout, stop_on_fail=stop_on_fail)
    finally:
        try:
            ser.close()
        except Exception:
            pass


def main(argv: list[str] | None = None) -> int:
    # Force line-buffered stdout so progress lines reach CI logs even if the
    # script exits early — pipe-buffered output gets dropped on sys.exit().
    try:
        sys.stdout.reconfigure(line_buffering=True)
    except Exception:
        pass

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", choices=list(hil.BOARD_REGISTRY.keys()),
                        help='Board role: "ci" or "dev".')
    parser.add_argument("--hla-serial", default=hil.DEFAULT_HLA_SERIAL,
                        help="ST-LINK serial number (overridden by --board).")
    parser.add_argument("--timeout", type=int, default=20,
                        help="Per-pass serial-capture timeout, seconds.")
    parser.add_argument("--baseline", type=Path,
                        default=Path("tests/baselines/bootloader_verify.json"),
                        help="Path to verify-time baseline JSON.")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--junit-xml", default="verify-results.xml")
    args = parser.parse_args(argv)

    project_root = hil.get_project_root()
    hla_serial = hil.BOARD_REGISTRY[args.board] if args.board else args.hla_serial

    if args.skip_build:
        clean = (project_root / "build" / "apps" / "bootloader"
                 / "app_blinky_signed" / "app_blinky_signed.signed.bin")
        if not clean.exists():
            hil.log_error("No existing signed image found - build required")
            sys.stdout.flush()
            write_junit_error_xml(args.junit_xml,
                                  "No existing signed image found - build required")
            return 2
    else:
        clean = build_smoke_app(project_root)
        if not clean:
            sys.stdout.flush()
            write_junit_error_xml(args.junit_xml,
                                  "Failed to build app_blinky_signed")
            return 2

    # Erase metadata so floor=0 regardless of prior board state.
    hil.log_info("Erasing metadata for clean-slate verify test...")
    hil.openocd_run(hla_serial,
                    "flash erase_sector 0 1 1",
                    "flash erase_sector 0 2 2")

    # ----- Pass A: clean image -----
    hil.log_info("=== Pass A: clean signed image ===")
    clean_lines = run_pass(clean, hla_serial, args.timeout, stop_on_fail=False)
    if not clean_lines:
        sys.stdout.flush()
        write_junit_error_xml(args.junit_xml,
                              "Pass A: no serial output (flash or serial open failed)")
        return 2

    clean_ok, clean_fails = assert_clean_pass(clean_lines)
    cycles = parse_verify_cycles(clean_lines)

    baseline_status = "n/a"
    if cycles is not None:
        baseline_status = maybe_check_baseline(cycles, args.baseline)
        hil.log_info(f"Verify baseline: {baseline_status}")

    # ----- Pass B: payload-tampered image -----
    hil.log_info("=== Pass B: payload-tampered image ===")
    try:
        tampered = make_tampered_image(clean)
    except Exception as e:
        hil.log_error(f"Could not build tampered image: {e}")
        sys.stdout.flush()
        write_junit_error_xml(args.junit_xml,
                              f"Could not build tampered image: {e}")
        return 2

    # Erase slot B payload and metadata so the bootloader has no valid
    # fallback target — otherwise A/B fallback (Phase 1.7+) would boot
    # whatever valid image is left in slot B, making the tamper check
    # appear to succeed.
    hil.log_info("Erasing slot B so bootloader cannot fall back...")
    hil.openocd_run(hla_serial,
                    "flash erase_sector 0 2 2",
                    "flash erase_sector 0 6 6")

    tamper_lines = run_pass(tampered, hla_serial, args.timeout, stop_on_fail=True)
    try:
        tampered.unlink()
    except OSError:
        pass
    if not tamper_lines:
        sys.stdout.flush()
        write_junit_error_xml(args.junit_xml,
                              "Pass B: no serial output (flash or serial open failed)")
        return 2

    tamper_ok, tamper_fails = assert_tampered_pass(tamper_lines)

    # ----- Reflash the clean image so the next CI step starts from a sane slot -----
    hil.log_info("=== Restoring clean image ===")
    hil.flash_firmware(clean, hla_serial=hla_serial)

    write_junit_xml(args.junit_xml,
                    clean_ok, clean_fails, cycles,
                    tamper_ok, tamper_fails,
                    baseline_status)

    if clean_ok and tamper_ok:
        hil.log_success("Verify test passed (clean boots, tampered rejected)")
        return 0

    if not clean_ok:
        hil.log_error("Pass A (clean image) failed:")
        for f in clean_fails:
            hil.log_error(f"  {f}")
    if not tamper_ok:
        hil.log_error("Pass B (tampered image) failed:")
        for f in tamper_fails:
            hil.log_error(f"  {f}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
