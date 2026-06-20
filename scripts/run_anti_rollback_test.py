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
ROLLBACK_RE            = re.compile(r"BL:\s+rollback\s+ver=")
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
    # Perform ALL setup in a single halted OpenOCD session: erase metadata,
    # erase slot B, program slot A.  No intermediate resets that could let
    # the bootloader commit stale metadata.
    hil.log_info(f"Flashing {signed_a_v1.name} at {SLOT_A_BASE:#010x} via OpenOCD...")
    openocd(hla_serial,
            "flash erase_sector 0 1 1",   # erase metadata A (sector 1)
            "flash erase_sector 0 2 2",   # erase metadata B (sector 2)
            "flash erase_sector 0 6 6",   # erase slot B payload (sector 6)
            f"program {signed_a_v1} {SLOT_A_BASE:#010x} verify")

    port = hil.find_serial_port(hla_serial=hla_serial)
    if not port:
        return False, ["serial port not found"]

    # Open serial BEFORE resetting to capture the bootloader's first line.
    ser = open_serial(port)
    try:
        reset_run(hla_serial)
        def saw_boot_a(lines):
            for l in lines:
                m = VERIFY_OK_RE.search(l)
                if m and m.group(1) == "A":
                    return True
            return False
        lines, ok = capture_until(ser, saw_boot_a, timeout_s=10.0)
        if not ok:
            fails.append("did not see 'BL: verify ok slot=A' on first boot")
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

    # After STATUS=ok the chip self-resets into slot B.  The kernel's
    # USB-CDC buffer retains the post-boot output even though ota_send.py
    # closed the port.  Open the port and wait for "fc cleared" — this
    # guarantees bl_handshake_clear_fail_count() has finished its flash
    # write before we proceed to Pass 3 (which does a reset halt that
    # would corrupt sector 2 if the write was still in progress).
    # Do NOT force an extra reset here — that creates the exact race.
    time.sleep(0.5)
    ser = open_serial(port)
    try:
        FC_CLEARED = "APP: fc cleared"
        def saw_boot_b(lines):
            has_b = any("slot B" in l or "slot=B" in l for l in lines)
            has_fc = any(FC_CLEARED in l for l in lines)
            return has_b and has_fc
        lines, ok = capture_until(ser, saw_boot_b, timeout_s=12.0)
        has_b_boot = any("slot B" in l or "slot=B" in l for l in lines)
        has_app = any(BLINKY_ALIVE in l for l in lines)
        if not has_b_boot:
            fails.append("missing slot B boot evidence after upgrade OTA")
        if not has_app:
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
    # The backup domain is write-protected by default.  We must:
    #   1. Enable the PWR peripheral clock (RCC_APB1ENR bit 28)
    #   2. Set the DBP bit in PWR->CR (bit 8)
    # Only then can we write RTC->BKP0R at 0x40002850.
    # After `reset halt` the chip is in a fresh state so we set the
    # exact bits we need (no read-modify-write necessary).
    RCC_APB1ENR = 0x40023840
    PWR_CR      = 0x40007000
    RTC_BKP0R   = 0x40002850
    PWREN_BIT   = 1 << 28
    DBP_BIT     = 1 << 8
    openocd(hla_serial,
            f"mww {RCC_APB1ENR:#010x} {PWREN_BIT:#010x}",
            f"mww {PWR_CR:#010x} {DBP_BIT:#010x}",
            f"mww {RTC_BKP0R:#010x} {OTA_MAGIC:#010x}")
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
    if rc == 0:
        fails.append("ota_send.py succeeded on a downgrade — floor not enforced")
        return False, fails
    # rc=5 means STATUS_ROLLBACK_REJECTED; rc=4 means the receiver sent
    # a different non-OK status (e.g. PROTOCOL_ERROR if the verify timing
    # caused a state-machine desync).  Either way, the downgrade failed.
    hil.log_info(f"ota_send.py rejected downgrade (rc={rc})")

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
    # Flash image + program metadata in one halted session WITHOUT
    # resetting, so the bootloader never runs with stale state.
    # A.mc=2, B.mc=2 (from the upgrade pass) → floor = max(2,2) = 2.
    # A's image_version=1 < floor=2, so A is rejected.  B's v2 >= 2 passes.
    # Tie-break: equal counters → slot A tried first → rollback → fallback B.
    hil.log_info(f"Flashing {signed_a_v1.name} at {SLOT_A_BASE:#010x} via OpenOCD...")
    md_blob = make_slot_metadata(active=1, fail_count=0, monotonic=2)
    md_file = Path(tempfile.mkstemp(prefix="md_pass4_", suffix=".bin")[1])
    md_file.write_bytes(md_blob)
    try:
        openocd(hla_serial,
                f"program {signed_a_v1} {SLOT_A_BASE:#010x} verify",
                f"program {md_file} {SLOT_A_METADATA:#010x} verify")
    finally:
        md_file.unlink(missing_ok=True)

    port = hil.find_serial_port(hla_serial=hla_serial)
    if not port:
        return False, ["serial port not found"]

    # Open serial BEFORE resetting so we capture the bootloader's first line.
    ser = open_serial(port)
    try:
        reset_run(hla_serial)
        def saw_rollback_then_b(lines):
            saw_rb = any(ROLLBACK_RE.search(l) for l in lines)
            saw_b = any(VERIFY_OK_RE.search(l) and
                        VERIFY_OK_RE.search(l).group(1) == "B" for l in lines)
            return saw_rb and saw_b
        lines, ok = capture_until(ser, saw_rollback_then_b, timeout_s=12.0)
        saw_rb = any(ROLLBACK_RE.search(l) for l in lines)
        if not saw_rb:
            fails.append("missing 'BL: rollback ver=...' after force-flash")
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

    # --- Ensure the board has the Phase 1.9 bootloader on sector 0 ---
    # The anti-rollback test requires the floor/fail_count logic that was
    # added in Phase 1.9.  If the board still has an older bootloader,
    # flash the freshly-built one now.  Unlike other HIL tests that only
    # touch slot payloads, this test is meaningless without the matching
    # bootloader.
    hil.log_info("Checking bootloader version on sector 0...")
    try:
        subprocess.run(
            ["make", "EXAMPLE=bootloader"],
            cwd=project_root, check=True, timeout=180,
            capture_output=True,
        )
    except subprocess.CalledProcessError as e:
        hil.log_error(f"bootloader build failed (exit {e.returncode})")
        return 2
    loader_elf = (project_root / "build" / "apps" / "bootloader"
                  / "loader" / "loader.elf")
    if not loader_elf.exists():
        hil.log_error(f"bootloader ELF not found at {loader_elf}")
        return 2
    # Flash the bootloader and erase metadata sectors so no stale state
    # from a previous test run interferes with Pass 1.
    hil.log_info("Flashing Phase 1.9 bootloader to sector 0...")
    flash_cmd = ["openocd"]
    if hla_serial:
        flash_cmd += ["-c", f"hla_serial {hla_serial}"]
    flash_cmd += ["-f", "board/st_nucleo_f4.cfg",
                  "-c", "init", "-c", "reset halt",
                  "-c", "flash erase_sector 0 1 2",
                  "-c", f"program {loader_elf} verify reset exit"]
    try:
        subprocess.run(flash_cmd, cwd=project_root, check=True,
                       capture_output=True, timeout=30)
    except subprocess.CalledProcessError as e:
        hil.log_error(f"bootloader flash failed: {e.stderr.decode()[:200] if e.stderr else ''}")
        return 2
    hil.log_success("Phase 1.9 bootloader flashed to sector 0")
    time.sleep(1.0)

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

    # Clean up: erase metadata sectors so the next CI run starts with a
    # blank slate.  Without this, the elevated floor from Pass 2/4 would
    # cause the boot smoke test (which uses image_version=1) to fail on
    # the subsequent CI run.
    hil.log_info("Cleaning up: erasing metadata sectors...")
    openocd(hla_serial,
            "flash erase_sector 0 1 1",
            "flash erase_sector 0 2 2")

    write_junit(args.junit_xml, results)
    return 0 if all(ok for _, ok, _ in results) else 1


if __name__ == "__main__":
    sys.exit(main())
