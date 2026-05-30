#!/usr/bin/env python3
"""Plan 001 Phase 1.5 boot-smoke test.

Flashes app_blinky_signed.signed.bin to slot A, resets the board, and asserts
that both the bootloader and the app emit their expected boot-log lines on
USART2.  This complements the existing cli_simple HIL run by exercising the
header-parse + jump path directly with a minimal app — if cli_simple ever
grows hooks that mask a bootloader regression, this test still catches it.

Exit codes:
    0  bootloader logged "BL: jumping to slot A" AND app logged
       "APP: blinky alive" within the timeout
    1  one or both lines were missing
    2  build / flash / serial error
"""

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path
from xml.etree.ElementTree import Element, SubElement, ElementTree, indent

# Re-use the board registry from run_hil_tests.py.
sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_hil_tests as hil  # noqa: E402

EXPECTED_LINES = [
    "BL: jumping to slot A",
    "APP: blinky alive",
]


def build_smoke_app(project_root: Path) -> Path:
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


def capture_boot_lines(port: str, timeout: int) -> list[str]:
    """Open the serial port, reset the board, and capture lines until timeout."""
    try:
        import serial
    except ImportError:
        hil.log_error("pyserial not installed. Run: pip3 install pyserial")
        return []

    lines: list[str] = []
    try:
        ser = serial.Serial(port, 115200, timeout=2)
        ser.reset_input_buffer()
        # The flash step ends with `reset` so the bootloader is already
        # running.  Give it a moment to push UART output, then drain.
        time.sleep(0.5)

        deadline = time.time() + timeout
        while time.time() < deadline:
            if ser.in_waiting:
                line = ser.readline().decode("utf-8", errors="ignore").rstrip()
                if line:
                    lines.append(line)
                    print(f"  {line}")
                    # Early exit once both expected lines have been seen.
                    if all(any(needle in s for s in lines) for needle in EXPECTED_LINES):
                        break
            else:
                time.sleep(0.05)
        ser.close()
    except Exception as e:
        hil.log_error(f"Serial error: {e}")
    return lines


def write_junit_xml(path: str, lines: list[str], success: bool) -> None:
    suites = Element("testsuites")
    suite = SubElement(suites, "testsuite", name="Boot smoke",
                       tests=str(len(EXPECTED_LINES)),
                       failures=str(0 if success else len(EXPECTED_LINES)),
                       errors="0", time="0")
    for needle in EXPECTED_LINES:
        tc = Element("testcase", classname="boot_smoke", name=needle, time="0")
        if not any(needle in s for s in lines):
            SubElement(tc, "failure", message=f"line not seen: {needle!r}")
        suite.append(tc)
    indent(suites)
    ElementTree(suites).write(path, encoding="unicode", xml_declaration=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", choices=list(hil.BOARD_REGISTRY.keys()),
                        help='Board role: "ci" or "dev".')
    parser.add_argument("--hla-serial", default=hil.DEFAULT_HLA_SERIAL,
                        help="ST-LINK serial number (overridden by --board).")
    parser.add_argument("--timeout", type=int, default=10,
                        help="Seconds to wait for both expected lines.")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--skip-flash", action="store_true")
    parser.add_argument("--junit-xml", default="boot-smoke-results.xml")
    args = parser.parse_args(argv)

    project_root = hil.get_project_root()
    hla_serial = hil.BOARD_REGISTRY[args.board] if args.board else args.hla_serial

    if args.skip_build:
        signed = (project_root / "build" / "apps" / "bootloader"
                  / "app_blinky_signed" / "app_blinky_signed.signed.bin")
        if not signed.exists():
            hil.log_error("No existing signed image found - build required")
            return 2
    else:
        signed = build_smoke_app(project_root)
        if not signed:
            return 2

    if not args.skip_flash:
        if not hil.flash_firmware(signed, hla_serial=hla_serial):
            return 2

    time.sleep(2)  # let the chip reset and start running

    port = hil.find_serial_port(hla_serial=hla_serial)
    if not port:
        hil.log_error("Serial port not found")
        return 2

    lines = capture_boot_lines(port, args.timeout)
    success = all(any(needle in s for s in lines) for needle in EXPECTED_LINES)

    write_junit_xml(args.junit_xml, lines, success)

    if success:
        hil.log_success("Boot smoke test passed")
        return 0
    hil.log_error("Boot smoke test failed:")
    for needle in EXPECTED_LINES:
        if not any(needle in s for s in lines):
            hil.log_error(f"  missing line: {needle!r}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
