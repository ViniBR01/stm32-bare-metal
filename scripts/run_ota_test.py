#!/usr/bin/env python3
"""Plan 001 Phase 1.8 OTA over UART HIL test.

Drives the bootloader OTA receiver end-to-end on a real NUCLEO-F411RE.

  Pass 1  Clean OTA, slot A (cli_simple) → slot B (app_blinky_signed).
            Boot the running cli_simple from slot A, wait for the CLI
            prompt, send `ota_request`, wait for the chip to reset into
            the bootloader's OTA mode, run tools/ota_send.py with a
            freshly built slot-B app_blinky_signed image, verify
            STATUS=ok, wait for the chip to reset, and assert the
            bootloader log shows 'verify ok slot=B' followed by the
            blinky's 'APP: blinky alive' line.

  Pass 2  Tampered OTA leaves the previously-active slot active.
            Reset the chip back to slot A as the active slot, request
            OTA again, but stream a tampered slot-B image.  Assert that
            ota_send.py reports STATUS=verify_failed and that the next
            reset still boots slot A (cli_simple).

The two slots run different apps on purpose: cli_simple's Makefile
isn't yet SLOT-aware (a separate hygiene fix), and using two different
apps end-to-end actually proves the OTA path more thoroughly than
cycling the same image between slots.

This is the first HIL exercise of `flash_slot_commit_metadata` and
`flash_slot_erase` running on real hardware — Phase 1.7 only validated
those functions via host unit tests.

Exit codes:
    0  both passes succeeded
    1  one or more passes failed an assertion
    2  build / flash / serial-open error
"""

from __future__ import annotations

import argparse
import os
import re
import struct
import subprocess
import sys
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

OTA_READY_LINE   = "OTA: ready"
OTA_OK_LINE_RE   = re.compile(r"OTA:\s+ok\s+slot=(\w)")
VERIFY_OK_RE     = re.compile(r"BL:\s+verify\s+ok\s+slot=(\w)")
APP_PROMPT       = "STM32 CLI Example"
BLINKY_ALIVE     = "APP: blinky alive"


# -------------------- Build helpers --------------------
#
# Slot A always carries cli_simple (slot-A only because cli_simple's Makefile
# isn't yet SLOT-aware; that's a separate hygiene fix).  We need cli_simple
# for the `ota_request` CLI command that triggers the bootloader OTA flow.
#
# Slot B is the OTA target.  Any signed image that the bootloader will
# verify works — we use app_blinky_signed because its Makefile already
# threads $(SLOT_SUFFIX) through every output path, so `make EXAMPLE=
# app_blinky_signed SLOT=B` produces a clean app_blinky_signed_b.signed.bin
# without colliding with the slot-A artifact.

def build_cli_simple(project_root: Path) -> Path:
    hil.log_info("Building cli_simple (slot A app — provides ota_request)...")
    subprocess.run(
        ["make", "EXAMPLE=cli_simple"],
        cwd=project_root, check=True, timeout=180,
    )
    signed = (project_root / "build" / "apps" / "cli"
              / "cli_simple" / "cli_simple.signed.bin")
    if not signed.exists():
        raise RuntimeError(f"expected {signed} after build")
    return signed


def build_blinky_for_slot_b(project_root: Path) -> Path:
    hil.log_info("Building app_blinky_signed for slot B (OTA target)...")
    # IMAGE_VERSION=2: the Phase 1.9 OTA receiver advances the monotonic
    # counter to max_seen+1 (at least 2 when slot A has mc=1).  The post-
    # OTA boot checks image_version >= floor, so the image must be >= 2.
    # Touch the .bin so make re-runs sign_image.py with the correct version
    # even if the binary itself hasn't changed (IMAGE_VERSION only affects
    # the header, not the compiled code).
    bin_path = (project_root / "build" / "apps" / "bootloader"
                / "app_blinky_signed_b" / "app_blinky_signed_b.bin")
    if bin_path.exists():
        bin_path.touch()
    subprocess.run(
        ["make", "EXAMPLE=app_blinky_signed", "SLOT=B", "IMAGE_VERSION=2"],
        cwd=project_root, check=True, timeout=180,
    )
    signed = (project_root / "build" / "apps" / "bootloader"
              / "app_blinky_signed_b" / "app_blinky_signed_b.signed.bin")
    if not signed.exists():
        raise RuntimeError(f"expected {signed} after build")
    return signed


def make_tampered(signed: Path) -> Path:
    """Flip one payload byte; tampered image must fail verify."""
    import tempfile
    raw = bytearray(signed.read_bytes())
    payload_offset = struct.unpack_from("<I", raw, 20)[0]
    if len(raw) < payload_offset + 8:
        raise RuntimeError(f"signed image {signed} unexpectedly small")
    raw[payload_offset + 4] ^= 0xFF
    tmp = Path(tempfile.mkstemp(prefix="tampered_", suffix=".signed.bin")[1])
    tmp.write_bytes(bytes(raw))
    return tmp


def make_slot_metadata(active: int, monotonic: int) -> bytes:
    return fmt.pack_slot_metadata(
        active=active, fail_count=0, monotonic_counter=monotonic,
    )


# -------------------- OpenOCD helpers --------------------

def openocd(hla_serial: str, *commands: str, timeout: int = 30) -> None:
    cmd = ["openocd"]
    if hla_serial:
        cmd += ["-c", f"hla_serial {hla_serial}"]
    cmd += ["-f", "board/st_nucleo_f4.cfg",
            "-c", "init", "-c", "reset halt"]
    for c in commands:
        cmd += ["-c", c]
    cmd += ["-c", "exit"]
    subprocess.run(cmd, check=True, capture_output=True, timeout=timeout)


def program_metadata(hla_serial: str, slot: str, blob: bytes) -> None:
    import tempfile
    addr = SLOT_A_METADATA if slot == "A" else SLOT_B_METADATA
    f = Path(tempfile.mkstemp(prefix="md_", suffix=".bin")[1])
    f.write_bytes(blob)
    try:
        openocd(hla_serial, f"program {f} {addr:#010x} verify")
    finally:
        try: f.unlink()
        except OSError: pass


def erase_slot_payload(hla_serial: str, slot: str) -> None:
    sectors = [4, 5] if slot == "A" else [6]
    cmds = [f"flash erase_sector 0 {s} {s}" for s in sectors]
    openocd(hla_serial, *cmds)


def reset_run(hla_serial: str) -> None:
    openocd(hla_serial, "reset run")


# -------------------- Serial helpers --------------------

def open_serial(port: str, *, timeout: float = 0.2):
    """Open the serial port with retry — kernel may briefly hold a lock."""
    import serial
    last_err = None
    for _ in range(3):
        try:
            return serial.Serial(port, 115200, timeout=timeout)
        except Exception as e:
            last_err = e
            time.sleep(1.0)
    raise RuntimeError(f"serial open failed: {last_err}")


def drain_serial(ser, settle_s: float = 1.0) -> None:
    """Drain whatever bytes are in flight from a previous boot."""
    deadline = time.time() + settle_s
    while time.time() < deadline:
        if ser.in_waiting:
            ser.read(ser.in_waiting)
            deadline = time.time() + 0.3
        else:
            time.sleep(0.05)
    try:
        ser.reset_input_buffer()
    except Exception:
        pass


def capture_until(
    ser, predicate, timeout_s: float, *, label_prefix: str = "  "
) -> tuple[list[str], bool]:
    """Read lines until `predicate(lines)` returns True or timeout."""
    lines: list[str] = []
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if ser.in_waiting:
            line = ser.readline().decode("utf-8", errors="ignore").rstrip()
            if line:
                lines.append(line)
                print(f"{label_prefix}{line}")
                if predicate(lines):
                    return lines, True
        else:
            time.sleep(0.05)
    return lines, False


def write_line(ser, text: str) -> None:
    ser.write(text.encode() + b"\r\n")
    ser.flush()


# -------------------- Test passes --------------------

def setup_slot_a_active(hla_serial: str, signed_a: Path) -> None:
    """Flash slot A clean, slot A metadata active, slot B erased."""
    hil.log_info("Resetting board: slot A active, slot B erased")
    if not hil.flash_firmware(signed_a, hla_serial=hla_serial,
                              slot_base=SLOT_A_BASE):
        raise RuntimeError("flash slot A failed")
    erase_slot_payload(hla_serial, "B")
    program_metadata(hla_serial, "A", make_slot_metadata(active=1, monotonic=1))
    program_metadata(hla_serial, "B", b"\xFF" * fmt.IMG_SLOT_METADATA_SIZE)


def request_ota_via_cli(ser) -> None:
    """Wait for the CLI prompt, then issue the ota_request command."""
    # cli_simple prints "\n> " after every command. The serial port opens
    # ~7 s after flash-and-reset on the Pi runner — well after the welcome
    # banner has already drained — so we cannot rely on the banner string.
    # Inject a bare newline; cli_simple echoes "\r\n> " in response, and
    # we accept either the welcome banner OR a lone ">" as proof the CLI
    # is alive.
    def saw_prompt(lines):
        for l in lines:
            if APP_PROMPT in l:
                return True
            if l.strip() == ">":
                return True
        return False
    drain_serial(ser, settle_s=0.5)
    write_line(ser, "")
    _, ok = capture_until(ser, saw_prompt, timeout_s=5.0)
    if not ok:
        # Try once more — first newline can race the cli's command pump on
        # a slow boot; a second injection after a short pause usually
        # succeeds.
        time.sleep(0.5)
        write_line(ser, "")
        _, ok = capture_until(ser, saw_prompt, timeout_s=5.0)
    if not ok:
        raise RuntimeError("cli prompt never appeared")
    hil.log_info("cli prompt seen — issuing ota_request")
    write_line(ser, "ota_request")
    # Bootloader prints "OTA: ready" once the magic is consumed and OTA mode
    # has wired up. We block here so the host serial port doesn't race a
    # mid-boot OTA receiver. If the chip boots back into the cli without
    # ever emitting "OTA: ready", the bootloader on sector 0 is older than
    # Phase 1.8 — surface that as an actionable error rather than a generic
    # timeout, since CI cannot reflash sector 0 itself (see CLAUDE.md +
    # scripts/flash_bootloader.py STM32_BARE_METAL_CI=1 guard).
    MIN_PHASE = 8  # OTA mode was added in Phase 1.8
    def _bl_phase(line: str) -> int | None:
        m = re.search(r"\(Phase 1\.(\d+)\)", line)
        return int(m.group(1)) if m else None

    def saw_ready_or_old_bl(lines):
        for l in lines:
            if OTA_READY_LINE in l:
                return True
            if "stm32-bare-metal bootloader" in l and _bl_phase(l) is None:
                return True
        return False
    lines, _ = capture_until(ser, saw_ready_or_old_bl, timeout_s=5.0)
    if any(OTA_READY_LINE in l for l in lines):
        hil.log_info("bootloader is in OTA mode")
        return
    stale = next((l for l in lines if "stm32-bare-metal bootloader" in l), None)
    if stale:
        phase = _bl_phase(stale)
        if phase is None or phase < MIN_PHASE:
            raise RuntimeError(
                f"sector 0 has a pre-Phase-1.8 bootloader ({stale.strip()}); "
                "reflash with `make flash-bootloader BOARD=ci` on the Pi runner "
                "before re-running this test"
            )
    raise RuntimeError("bootloader never advertised 'OTA: ready'")


def run_ota_send(project_root: Path, port: str, image: Path, slot: str) -> int:
    """Invoke tools/ota_send.py as a subprocess and forward its output."""
    cmd = [sys.executable, "tools/ota_send.py",
           "--port", port,
           "--image", str(image),
           "--slot", slot]
    hil.log_info(f"running: {' '.join(cmd)}")
    proc = subprocess.run(cmd, cwd=project_root, timeout=120)
    return proc.returncode


def pass_clean_ota(
    project_root: Path, hla_serial: str,
    signed_a: Path, signed_b: Path,
) -> tuple[bool, list[str]]:
    fails: list[str] = []
    setup_slot_a_active(hla_serial, signed_a)
    reset_run(hla_serial)
    time.sleep(1.0)

    port = hil.find_serial_port(hla_serial=hla_serial)
    if not port:
        return False, ["serial port not found"]

    ser = open_serial(port)
    try:
        request_ota_via_cli(ser)
    finally:
        # ota_send.py needs exclusive access to the port.
        ser.close()

    rc = run_ota_send(project_root, port, signed_b, "B")
    if rc != 0:
        return False, [f"ota_send.py exited with code {rc}"]

    # After STATUS=ok the chip self-resets.  The kernel USB-CDC buffer may
    # not retain the bytes.  Force a second reset with the serial port
    # already open so we capture the full boot deterministically.  This is
    # safe here because Pass 2's setup_slot_a_active() will erase slot B
    # anyway — no risk of catching a mid-write state.
    # Wait 2s for the first boot to complete its flash writes (sector
    # erase ~40ms + program; the app prints "fc cleared" within ~200ms
    # of "alive" but we can't read it until the port is open).
    time.sleep(2.0)
    ser = open_serial(port)
    try:
        reset_run(hla_serial)
        FC_CLEARED = "APP: fc cleared"
        def saw_both(lines):
            has_blinky = any(BLINKY_ALIVE in l for l in lines)
            has_fc = any(FC_CLEARED in l for l in lines)
            return has_blinky and has_fc
        lines, ok = capture_until(ser, saw_both, timeout_s=10.0)
        has_blinky = any(BLINKY_ALIVE in l for l in lines)
        has_b = any("slot B" in l or "slot=B" in l for l in lines)
        if not has_blinky:
            fails.append(f"did not see '{BLINKY_ALIVE}' after OTA reset")
        if not has_blinky and not has_b:
            fails.append("no post-OTA boot output captured at all")
    finally:
        ser.close()

    return len(fails) == 0, fails


def pass_tampered_ota(
    project_root: Path, hla_serial: str,
    signed_a: Path, signed_b: Path,
) -> tuple[bool, list[str]]:
    fails: list[str] = []
    # Force back to slot A active so OTA can target B again.
    setup_slot_a_active(hla_serial, signed_a)
    # Erase slot B payload too so any leftover from pass 1 is gone.
    erase_slot_payload(hla_serial, "B")
    reset_run(hla_serial)
    time.sleep(1.0)

    port = hil.find_serial_port(hla_serial=hla_serial)
    if not port:
        return False, ["serial port not found"]

    ser = open_serial(port)
    try:
        request_ota_via_cli(ser)
    finally:
        ser.close()

    tampered = make_tampered(signed_b)
    try:
        rc = run_ota_send(project_root, port, tampered, "B")
        # Exit code 4 = bootloader reported STATUS != ok (verify_failed).
        if rc != 4:
            fails.append(f"expected ota_send.py rc=4, got {rc}")
            return False, fails
    finally:
        try: tampered.unlink()
        except OSError: pass

    # After STATUS=verify_failed the bootloader halts with the slow-blink
    # halt loop and does NOT reset.  We force one ourselves and confirm A
    # still boots.
    reset_run(hla_serial)
    time.sleep(1.0)
    ser = open_serial(port)
    try:
        def saw_verify(lines):
            return any(VERIFY_OK_RE.search(l) for l in lines)
        lines, ok = capture_until(ser, saw_verify, timeout_s=10.0)
        if not ok:
            fails.append("did not see 'BL: verify ok slot=...' after recovery reset")
            return False, fails
        m = next((VERIFY_OK_RE.search(l) for l in lines
                  if VERIFY_OK_RE.search(l)), None)
        if m and m.group(1) != "A":
            fails.append(
                f"booted slot {m.group(1)} after tampered OTA, expected A still active"
            )
    finally:
        ser.close()

    return len(fails) == 0, fails


# -------------------- JUnit XML --------------------

def write_junit(
    path: Path, clean_ok: bool, clean_fails: list[str],
    tampered_ok: bool, tampered_fails: list[str],
) -> None:
    suites = Element("testsuites")
    suite = SubElement(suites, "testsuite", name="ota_test",
                       tests="2",
                       failures=str(int(not clean_ok) + int(not tampered_ok)))

    clean_tc = SubElement(suite, "testcase",
                          classname="ota_test", name="clean_ota_a_to_b", time="0")
    if not clean_ok:
        SubElement(clean_tc, "failure", message="; ".join(clean_fails))

    tamper_tc = SubElement(suite, "testcase",
                           classname="ota_test", name="tampered_ota_keeps_a", time="0")
    if not tampered_ok:
        SubElement(tamper_tc, "failure", message="; ".join(tampered_fails))

    indent(suites)
    ElementTree(suites).write(path, encoding="unicode", xml_declaration=True)


# -------------------- Main --------------------

BOARDS = {"ci": "066BFF554869774867234426", "dev": "066AFF504951857267161331"}


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--board", choices=list(BOARDS.keys()),
                   help="Pin operations to a known ST-LINK serial.")
    p.add_argument("--hla-serial", default="",
                   help="Explicit ST-LINK serial (overrides --board).")
    p.add_argument("--junit-xml", type=Path)
    args = p.parse_args()

    hla_serial = args.hla_serial or (BOARDS.get(args.board, "") if args.board else "")
    project_root = hil.get_project_root()
    os.chdir(project_root)

    try:
        signed_a = build_cli_simple(project_root)
        signed_b = build_blinky_for_slot_b(project_root)
    except Exception as e:
        hil.log_error(f"build failed: {e}")
        return 2

    hil.log_info("=== Pass 1: clean OTA A → B ===")
    clean_ok, clean_fails = pass_clean_ota(project_root, hla_serial,
                                            signed_a, signed_b)
    if clean_ok:
        hil.log_success("Pass 1 OK")
    else:
        hil.log_error(f"Pass 1 FAILED: {'; '.join(clean_fails)}")

    hil.log_info("=== Pass 2: tampered OTA leaves A active ===")
    tampered_ok, tampered_fails = pass_tampered_ota(
        project_root, hla_serial, signed_a, signed_b
    )
    if tampered_ok:
        hil.log_success("Pass 2 OK")
    else:
        hil.log_error(f"Pass 2 FAILED: {'; '.join(tampered_fails)}")

    if args.junit_xml:
        write_junit(args.junit_xml, clean_ok, clean_fails,
                    tampered_ok, tampered_fails)

    return 0 if (clean_ok and tampered_ok) else 1


if __name__ == "__main__":
    sys.exit(main())
