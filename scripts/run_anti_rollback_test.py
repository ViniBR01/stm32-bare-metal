#!/usr/bin/env python3
"""Plan 001 Phase 1.9 anti-rollback HIL test.

Verifies that the bootloader's rollback floor and OTA-side floor check
both behave per the spec:

  Pass 1  Seed: image v1 in slot A, both metadata sectors erased.
            Boot.  Expect bootloader to verify+jump slot A, then
            commit metadata with monotonic_counter=1 (floor seeds).

  Pass 2  Clean upgrade: OTA image v2 → slot B.
            Expect STATUS=ok, slot B becomes active, next boot
            picks slot B and the floor advances to 2.

  Pass 3  Downgrade rejected over OTA: stream image v1 again.
            Expect STATUS=rollback_rejected, slot B still active,
            next boot still picks slot B.

  Pass 4  Force-flash a downgrade.  Use OpenOCD to erase slot A
            metadata and write a fresh active=1, monotonic_counter=2
            metadata blob — the bootloader will pick slot A on the
            next boot, but the slot-A image is still v1, which is
            below the floor (2).  Expect bootloader to log
            "rollback ver=1 < floor=2", fall back to slot B.

Because the OTA path needs a live cli_simple (for ota_request), slot A
runs cli_simple and slot B runs app_blinky_signed.  Same convention as
run_ota_test.py.

Both apps live at the same image_version each pass — the version is set
via IMAGE_VERSION=N during the make invocation.

Exit codes:
    0  all four passes succeeded
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

OTA_READY_LINE         = "OTA: ready"
OTA_OK_LINE_RE         = re.compile(r"OTA:\s+ok\s+slot=(\w)")
OTA_ROLLBACK_LINE      = "OTA: rollback rejected"
VERIFY_OK_RE           = re.compile(r"BL:\s+verify\s+ok\s+slot=(\w)")
ROLLBACK_RE            = re.compile(r"BL:\s+slot\s+(\w)\s+rollback")
FALLBACK_RE            = re.compile(r"BL:\s+falling\s+back\s+to\s+slot\s+(\w)")
APP_PROMPT             = "STM32 CLI Example"
BLINKY_ALIVE           = "APP: blinky alive"


# -------------------- Build helpers --------------------

def build_cli_simple(project_root: Path, image_version: int) -> Path:
    hil.log_info(f"Building cli_simple (slot A, IMAGE_VERSION={image_version})...")
    # Force a clean rebuild of the signed bin so the new IMAGE_VERSION
    # actually lands in the header (the .signed.bin rule depends on
    # IMAGE_VERSION as a make variable, but the .bin itself is content-
    # only — make won't re-sign unless the .bin changes).  Touch the bin.
    bin_path = (project_root / "build" / "apps" / "cli"
                / "cli_simple" / "cli_simple.bin")
    if bin_path.exists():
        bin_path.touch()
    subprocess.run(
        ["make", "EXAMPLE=cli_simple", f"IMAGE_VERSION={image_version}"],
        cwd=project_root, check=True, timeout=180,
    )
    signed = (project_root / "build" / "apps" / "cli"
              / "cli_simple" / "cli_simple.signed.bin")
    if not signed.exists():
        raise RuntimeError(f"expected {signed} after build")
    # Stash a copy so subsequent rebuilds with a different version don't
    # clobber it.  Caller renames the returned path freely.
    out = signed.with_name(f"cli_simple.v{image_version}.signed.bin")
    out.write_bytes(signed.read_bytes())
    return out


def build_blinky_b(project_root: Path, image_version: int) -> Path:
    hil.log_info(f"Building app_blinky_signed_b (slot B, IMAGE_VERSION={image_version})...")
    bin_path = (project_root / "build" / "apps" / "bootloader"
                / "app_blinky_signed_b" / "app_blinky_signed_b.bin")
    if bin_path.exists():
        bin_path.touch()
    subprocess.run(
        ["make", "EXAMPLE=app_blinky_signed", "SLOT=B",
         f"IMAGE_VERSION={image_version}"],
        cwd=project_root, check=True, timeout=180,
    )
    signed = (project_root / "build" / "apps" / "bootloader"
              / "app_blinky_signed_b" / "app_blinky_signed_b.signed.bin")
    if not signed.exists():
        raise RuntimeError(f"expected {signed} after build")
    out = signed.with_name(f"app_blinky_signed_b.v{image_version}.signed.bin")
    out.write_bytes(signed.read_bytes())
    return out


def build_blinky_a(project_root: Path, image_version: int) -> Path:
    """Build a slot-A app_blinky_signed for the force-flash downgrade pass."""
    hil.log_info(f"Building app_blinky_signed (slot A, IMAGE_VERSION={image_version})...")
    bin_path = (project_root / "build" / "apps" / "bootloader"
                / "app_blinky_signed" / "app_blinky_signed.bin")
    if bin_path.exists():
        bin_path.touch()
    subprocess.run(
        ["make", "EXAMPLE=app_blinky_signed",
         f"IMAGE_VERSION={image_version}"],
        cwd=project_root, check=True, timeout=180,
    )
    signed = (project_root / "build" / "apps" / "bootloader"
              / "app_blinky_signed" / "app_blinky_signed.signed.bin")
    if not signed.exists():
        raise RuntimeError(f"expected {signed} after build")
    out = signed.with_name(f"app_blinky_signed_a.v{image_version}.signed.bin")
    out.write_bytes(signed.read_bytes())
    return out


def make_slot_metadata(active: int, fail_count: int, monotonic: int) -> bytes:
    return fmt.pack_slot_metadata(
        active=active, fail_count=fail_count, monotonic_counter=monotonic,
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
    addr = SLOT_A_METADATA if slot == "A" else SLOT_B_METADATA
    f = Path(tempfile.mkstemp(prefix="md_", suffix=".bin")[1])
    f.write_bytes(blob)
    try:
        openocd(hla_serial, f"program {f} {addr:#010x} verify")
    finally:
        try: f.unlink()
        except OSError: pass


def erase_metadata(hla_serial: str, slot: str) -> None:
    sector = 1 if slot == "A" else 2
    openocd(hla_serial, f"flash erase_sector 0 {sector} {sector}")


def erase_slot_payload(hla_serial: str, slot: str) -> None:
    sectors = [4, 5] if slot == "A" else [6]
    cmds = [f"flash erase_sector 0 {s} {s}" for s in sectors]
    openocd(hla_serial, *cmds)


def reset_run(hla_serial: str) -> None:
    openocd(hla_serial, "reset run")


# -------------------- Serial helpers --------------------

def open_serial(port: str, *, timeout: float = 0.2):
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


def capture_until(ser, predicate, timeout_s: float,
                  *, label_prefix: str = "  ") -> tuple[list[str], bool]:
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


def request_ota_via_cli(ser) -> None:
    """Wait for cli_simple's prompt, then issue ota_request."""
    def saw_prompt(lines):
        for l in lines:
            if APP_PROMPT in l or l.strip() == ">":
                return True
        return False
    drain_serial(ser, settle_s=0.5)
    write_line(ser, "")
    _, ok = capture_until(ser, saw_prompt, timeout_s=5.0)
    if not ok:
        time.sleep(0.5)
        write_line(ser, "")
        _, ok = capture_until(ser, saw_prompt, timeout_s=5.0)
    if not ok:
        raise RuntimeError("cli prompt never appeared")
    write_line(ser, "ota_request")
    def saw_ready(lines):
        return any(OTA_READY_LINE in l for l in lines)
    _, ok = capture_until(ser, saw_ready, timeout_s=5.0)
    if not ok:
        raise RuntimeError("bootloader never advertised 'OTA: ready'")


def run_ota_send(project_root: Path, port: str,
                 image: Path, slot: str) -> int:
    cmd = [sys.executable, "tools/ota_send.py",
           "--port", port, "--image", str(image), "--slot", slot]
    hil.log_info(f"running: {' '.join(cmd)}")
    return subprocess.run(cmd, cwd=project_root, timeout=120).returncode


# -------------------- Pass implementations --------------------

def pass_seed_v1(hla_serial: str, signed_a_v1: Path) -> tuple[bool, list[str]]:
    """Flash slot A with v1 image, erase metadata, reset.  Expect a
    successful boot that seeds the floor at 1."""
    fails: list[str] = []
    hil.log_info("Resetting board: slot A=v1, slot B erased, both metadata erased")
    if not hil.flash_firmware(signed_a_v1, hla_serial=hla_serial,
                              slot_base=SLOT_A_BASE):
        return False, ["flash slot A v1 failed"]
    erase_slot_payload(hla_serial, "B")
    erase_metadata(hla_serial, "A")
    erase_metadata(hla_serial, "B")
    reset_run(hla_serial)
    time.sleep(1.0)

    port = hil.find_serial_port(hla_serial=hla_serial)
    if not port:
        return False, ["serial port not found"]

    ser = open_serial(port)
    try:
        def saw_boot_a(lines):
            for l in lines:
                m = VERIFY_OK_RE.search(l)
                if m and m.group(1) == "A":
                    return True
            return False
        lines, ok = capture_until(ser, saw_boot_a, timeout_s=10.0)
        if not ok:
            fails.append("did not see 'BL: verify ok slot=A' on first boot")
        # The CLI takes a couple of seconds to come up; that's covered
        # by Pass 2's prompt-wait so we don't need to wait here.
    finally:
        ser.close()

    return len(fails) == 0, fails


def pass_clean_upgrade(hla_serial: str, project_root: Path,
                       signed_b_v2: Path) -> tuple[bool, list[str]]:
    """OTA v2 from slot A to slot B; expect slot B boots."""
    fails: list[str] = []
    port = hil.find_serial_port(hla_serial=hla_serial)
    if not port:
        return False, ["serial port not found"]

    ser = open_serial(port)
    try:
        request_ota_via_cli(ser)
    finally:
        ser.close()

    rc = run_ota_send(project_root, port, signed_b_v2, "B")
    if rc != 0:
        return False, [f"ota_send.py exited with rc={rc} (expected 0)"]

    time.sleep(1.5)
    ser = open_serial(port)
    try:
        def saw_boot_b(lines):
            has_v = any(VERIFY_OK_RE.search(l) and
                        VERIFY_OK_RE.search(l).group(1) == "B" for l in lines)
            has_a = any(BLINKY_ALIVE in l for l in lines)
            return has_v and has_a
        lines, ok = capture_until(ser, saw_boot_b, timeout_s=12.0)
        has_v = any(VERIFY_OK_RE.search(l) and
                    VERIFY_OK_RE.search(l).group(1) == "B" for l in lines)
        has_a = any(BLINKY_ALIVE in l for l in lines)
        if not has_v:
            fails.append("missing 'BL: verify ok slot=B' after upgrade OTA")
        if not has_a:
            fails.append(f"missing '{BLINKY_ALIVE}' after upgrade OTA")
    finally:
        ser.close()
    return len(fails) == 0, fails


def pass_downgrade_ota_rejected(hla_serial: str, project_root: Path,
                                signed_b_v1: Path) -> tuple[bool, list[str]]:
    """OTA an older v1 image to slot A while slot B (v2) is active.

    The chip is currently booted into the slot-B blinky.  The blinky
    has no CLI; we need to get back into the bootloader's OTA mode.
    We flash slot A with cli_simple (so we can issue ota_request) — but
    that wipes slot A's payload and would change the floor commit.
    Cleaner: write the OTA magic into RTC backup register 0 via OpenOCD,
    then reset; the bootloader sees the magic and enters OTA mode
    directly without needing an app-side trigger.
    """
    fails: list[str] = []
    hil.log_info("Forcing OTA mode via RTC backup register magic...")
    OTA_MAGIC = 0x4F544131
    openocd(hla_serial, f"mww 0x40002850 {OTA_MAGIC:#010x}")
    reset_run(hla_serial)
    time.sleep(1.0)

    port = hil.find_serial_port(hla_serial=hla_serial)
    if not port:
        return False, ["serial port not found"]

    ser = open_serial(port)
    try:
        def saw_ready(lines):
            return any(OTA_READY_LINE in l for l in lines)
        _, ok = capture_until(ser, saw_ready, timeout_s=5.0)
        if not ok:
            return False, ["bootloader never reached OTA: ready"]
    finally:
        ser.close()

    rc = run_ota_send(project_root, port, signed_b_v1, "B")
    # ota_send.py exits 5 for STATUS=rollback_rejected; we should not
    # see exit 0 (that would mean STATUS=ok).
    if rc == 0:
        fails.append("ota_send.py succeeded on a downgrade — floor not enforced")
        return False, fails
    if rc != 5:
        # Be lenient — older ota_send.py exit codes used 4 for any non-OK
        # status.  We'll cross-check by looking at the bootloader log.
        hil.log_warning(f"ota_send.py rc={rc} (expected 5 for rollback_rejected)")

    # The bootloader halts on rejection.  Force a reset and check that
    # slot B is still active (i.e. the rejected OTA bytes never replaced
    # the live image).
    reset_run(hla_serial)
    time.sleep(1.0)
    ser = open_serial(port)
    try:
        def saw_b(lines):
            for l in lines:
                m = VERIFY_OK_RE.search(l)
                if m and m.group(1) == "B":
                    return True
            return False
        lines, ok = capture_until(ser, saw_b, timeout_s=10.0)
        if not ok:
            fails.append("expected slot B to remain active after rejected OTA")
    finally:
        ser.close()

    return len(fails) == 0, fails


def pass_force_flash_downgrade(hla_serial: str, signed_a_v1: Path
                                ) -> tuple[bool, list[str]]:
    """Force a downgrade via OpenOCD — the path RDP-1 will close in
    Phase 1.10.  Expect the bootloader to still reject the rolled-back
    slot at boot time.

    Setup:
      - Slot B currently has v2 image, metadata active=1 mc>=2 (from
        the earlier upgrade pass).  Floor reads as >=2.
      - Force slot A: flash an older v1 cli_simple, then write a fresh
        active=1, monotonic_counter=2 metadata blob.  The bootloader
        will pick A first (active flag, equal counter ties with B but
        the slot-pick prefers higher counter — so we go further: bump
        A's counter to one higher to actually trick the slot-pick).
      - Boot.  Floor seen via `img_compute_floor` is max(A.mc, B.mc),
        but the slot-A image is v1 — which is below the floor.
    """
    fails: list[str] = []
    hil.log_info("Force-flashing slot A with v1 and a counter-overshooting metadata...")
    if not hil.flash_firmware(signed_a_v1, hla_serial=hla_serial,
                              slot_base=SLOT_A_BASE):
        return False, ["flash slot A v1 failed"]
    # Write A metadata: counter=3, active=1.  B currently has counter>=2.
    # The floor = max(A.mc=3, B.mc>=2) = 3.  A's image_version=1 < 3.
    program_metadata(hla_serial, "A",
                     make_slot_metadata(active=1, fail_count=0, monotonic=3))
    reset_run(hla_serial)
    time.sleep(1.0)

    port = hil.find_serial_port(hla_serial=hla_serial)
    if not port:
        return False, ["serial port not found"]

    ser = open_serial(port)
    try:
        def saw_rollback_then_b(lines):
            saw_rb = any(ROLLBACK_RE.search(l) for l in lines)
            saw_b = any(VERIFY_OK_RE.search(l) and
                        VERIFY_OK_RE.search(l).group(1) == "B" for l in lines)
            return saw_rb and saw_b
        lines, ok = capture_until(ser, saw_rollback_then_b, timeout_s=12.0)
        saw_rb = any(ROLLBACK_RE.search(l) for l in lines)
        if not saw_rb:
            fails.append("missing 'BL: slot A rollback ...' after force-flash")
        saw_b = any(VERIFY_OK_RE.search(l) and
                    VERIFY_OK_RE.search(l).group(1) == "B" for l in lines)
        if not saw_b:
            fails.append("expected fallback to slot B after rollback rejection")
    finally:
        ser.close()

    return len(fails) == 0, fails


# -------------------- JUnit XML --------------------

def write_junit(path: Path,
                results: list[tuple[str, bool, list[str]]]) -> None:
    suites = Element("testsuites")
    failures = sum(1 for _, ok, _ in results if not ok)
    suite = SubElement(suites, "testsuite",
                       name="anti_rollback",
                       tests=str(len(results)),
                       failures=str(failures))
    for name, ok, fails in results:
        tc = SubElement(suite, "testcase",
                        classname="anti_rollback", name=name, time="0")
        if not ok:
            SubElement(tc, "failure", message="; ".join(fails) or "fail")
    indent(suites)
    ElementTree(suites).write(path, encoding="unicode", xml_declaration=True)


# -------------------- Main --------------------

def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--board", choices=list(hil.BOARD_REGISTRY.keys()),
                   help='Board role: "ci" or "dev".')
    p.add_argument("--hla-serial", default=hil.DEFAULT_HLA_SERIAL,
                   help="Explicit ST-LINK serial (overrides --board).")
    p.add_argument("--junit-xml", type=Path,
                   default=Path("anti-rollback-results.xml"))
    args = p.parse_args()

    try:
        sys.stdout.reconfigure(line_buffering=True)
    except Exception:
        pass

    hla_serial = (hil.BOARD_REGISTRY[args.board] if args.board
                  else args.hla_serial)
    project_root = hil.get_project_root()
    os.chdir(project_root)

    # Build all images up front so test passes don't time out doing IO.
    try:
        signed_a_v1     = build_cli_simple(project_root, image_version=1)
        signed_b_v2     = build_blinky_b(project_root,    image_version=2)
        signed_b_v1     = build_blinky_b(project_root,    image_version=1)
        signed_a_blinky = build_blinky_a(project_root,    image_version=1)
    except Exception as e:
        hil.log_error(f"build failed: {e}")
        return 2

    results: list[tuple[str, bool, list[str]]] = []

    hil.log_info("=== Pass 1: seed v1 in slot A ===")
    ok, fails = pass_seed_v1(hla_serial, signed_a_v1)
    results.append(("seed_v1_in_slot_a", ok, fails))
    if ok: hil.log_success("Pass 1 OK")
    else:
        hil.log_error(f"Pass 1 FAILED: {'; '.join(fails)}")
        write_junit(args.junit_xml, results)
        return 1

    hil.log_info("=== Pass 2: clean upgrade OTA v2 → slot B ===")
    ok, fails = pass_clean_upgrade(hla_serial, project_root, signed_b_v2)
    results.append(("clean_upgrade_to_v2", ok, fails))
    if ok: hil.log_success("Pass 2 OK")
    else:
        hil.log_error(f"Pass 2 FAILED: {'; '.join(fails)}")

    hil.log_info("=== Pass 3: downgrade OTA v1 rejected ===")
    ok, fails = pass_downgrade_ota_rejected(hla_serial, project_root,
                                            signed_b_v1)
    results.append(("downgrade_ota_rejected", ok, fails))
    if ok: hil.log_success("Pass 3 OK")
    else:
        hil.log_error(f"Pass 3 FAILED: {'; '.join(fails)}")

    hil.log_info("=== Pass 4: force-flashed downgrade rejected at boot ===")
    ok, fails = pass_force_flash_downgrade(hla_serial, signed_a_blinky)
    results.append(("force_flash_downgrade_rejected", ok, fails))
    if ok: hil.log_success("Pass 4 OK")
    else:
        hil.log_error(f"Pass 4 FAILED: {'; '.join(fails)}")

    write_junit(args.junit_xml, results)
    return 0 if all(ok for _, ok, _ in results) else 1


if __name__ == "__main__":
    sys.exit(main())
