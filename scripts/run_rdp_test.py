#!/usr/bin/env python3
"""Plan 001 Phase 1.10 — RDP-1 HIL test (dev board only).

Drives the dev NUCLEO board through L0 → L1 → L0, with assertions at
each step.  L1 → L0 mass-erases user flash, so the test reflashes the
bootloader and the slot-A app at the end so the rig is left ready for
normal HIL workflows.

Refuses to run on the CI board.  Refuses to run when
STM32_BARE_METAL_CI=1.  This test is NOT part of the standard hil-tests
job — it lives under a manual workflow_dispatch trigger because it
cycles option bytes and would block every other HIL test that
follows it on the same chip.

Flow:
  1. Read options.  Require starting state == L0.
  2. set_rdp.py --level 1 --confirm.  Confirm L1 read-back.
  3. openocd dump_image of sector 0 — expect failure or all-FF data.
  4. Confirm bootloader still UART-prints over USART2.
  5. set_rdp.py --level 0 --confirm.  Wait for mass-erase.  Confirm
     sector 0 reads as 0xFF.
  6. Reflash bootloader + slot-A app to leave the board working.

Exit codes:
    0  full happy path
    1  test-flow assertion failed
    2  prerequisite missing (chip not at L0, build artifacts absent,
       wrong serial, etc.)
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from xml.etree.ElementTree import Element, SubElement, ElementTree, indent

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_hil_tests as hil  # noqa: E402

OPENOCD_CFG = "board/st_nucleo_f4.cfg"
SECTOR_0_BASE = 0x08000000
SECTOR_0_SIZE = 16 * 1024
SLOT_A_BASE = 0x08010000

# Lines we expect to see from the bootloader on a normal boot, after we
# arm RDP-1 but before we regress to L0.  Phase 1.5+ banner.
BOOTLOADER_BANNER_RE = re.compile(r"BL:\s+stm32-bare-metal\s+bootloader")


def project_root() -> Path:
    return Path(__file__).resolve().parent.parent


# -------------------- result tracking --------------------

class Step:
    """One named step in the flow.  Tracks pass/fail + a human message."""
    def __init__(self, name: str, classname: str = "rdp_test"):
        self.name = name
        self.classname = classname
        self.ok: bool | None = None
        self.message = ""

    def succeed(self, msg: str = "") -> None:
        self.ok = True
        self.message = msg
        hil.log_success(f"{self.name}: ok" + (f" — {msg}" if msg else ""))

    def fail(self, msg: str) -> None:
        self.ok = False
        self.message = msg
        hil.log_error(f"{self.name}: FAIL — {msg}")


# -------------------- openocd helpers --------------------

def run_openocd(hla_serial: str, *commands: str,
                timeout: int = 30) -> tuple[int, str, str]:
    cmd = ["openocd"]
    if hla_serial:
        cmd += ["-c", f"adapter serial {hla_serial}"]
    cmd += ["-f", OPENOCD_CFG, "-c", "init", "-c", "reset halt"]
    for c in commands:
        cmd += ["-c", c]
    cmd += ["-c", "exit"]
    try:
        result = subprocess.run(
            cmd, cwd=project_root(),
            capture_output=True, text=True, timeout=timeout,
        )
        return result.returncode, result.stdout, result.stderr
    except subprocess.TimeoutExpired as e:
        return 124, e.stdout or "", e.stderr or f"timeout after {timeout}s"


def dump_sector0(hla_serial: str) -> tuple[bool, bytes | None, str]:
    """Try to dump sector 0 to a temp file via openocd.

    Returns (succeeded, contents_bytes_or_None, message).

    Under RDP-1 the dump either fails outright or returns all-`FF`.
    Both are valid pass signals for the L1 step; the *L0* step instead
    requires this to succeed AND read as all-`FF` (because the mass
    erase completed).
    """
    fd, path = tempfile.mkstemp(prefix="rdp_dump_", suffix=".bin")
    os.close(fd)
    try:
        rc, stdout, stderr = run_openocd(
            hla_serial,
            f"dump_image {path} {SECTOR_0_BASE:#010x} {SECTOR_0_SIZE}",
            timeout=30,
        )
        if rc != 0:
            return False, None, (stderr or stdout)[-500:]
        try:
            data = Path(path).read_bytes()
        except OSError as e:
            return False, None, f"could not read dump file: {e}"
        if len(data) != SECTOR_0_SIZE:
            return False, data, f"dump short: got {len(data)} bytes"
        return True, data, "ok"
    finally:
        try: os.unlink(path)
        except OSError: pass


def all_ff(data: bytes) -> bool:
    return all(b == 0xFF for b in data)


# -------------------- step implementations --------------------

def step_read_l0(hla_serial: str) -> tuple[Step, bool]:
    s = Step("step_1_chip_starts_at_l0")
    rc = subprocess.run(
        ["python3", "scripts/set_rdp.py",
         "--hla-serial", hla_serial, "--status"],
        cwd=project_root(), capture_output=True, text=True, timeout=20,
    )
    text = rc.stdout + rc.stderr
    print(text)
    m = re.search(r"RDP level:\s*(\d)", text)
    if rc.returncode != 0 or not m:
        s.fail(f"could not read RDP level (exit {rc.returncode})")
        return s, False
    level = int(m.group(1))
    if level != 0:
        s.fail(f"chip is at RDP level {level}, expected 0")
        return s, False
    s.succeed("starting at L0")
    return s, True


def step_engage_l1(hla_serial: str) -> tuple[Step, bool]:
    s = Step("step_2_set_level_1")
    rc = subprocess.run(
        ["python3", "scripts/set_rdp.py",
         "--hla-serial", hla_serial, "--level", "1", "--confirm"],
        cwd=project_root(), capture_output=True, text=True, timeout=60,
    )
    text = rc.stdout + rc.stderr
    print(text)
    if rc.returncode != 0:
        s.fail(f"set_rdp.py --level 1 returned {rc.returncode}")
        return s, False
    if "RDP level is now 1" not in text:
        s.fail("set_rdp.py did not confirm L1 readback")
        return s, False
    s.succeed("RDP-1 engaged")
    return s, True


def step_read_blocked(hla_serial: str) -> tuple[Step, bool]:
    s = Step("step_3_dump_sector0_blocked_or_ff")
    succeeded, data, msg = dump_sector0(hla_serial)
    if not succeeded:
        # OpenOCD refused outright — that's a valid outcome under RDP-1.
        s.succeed(f"dump_image refused (expected under RDP-1): {msg.strip()}")
        return s, True
    if data is None:
        s.fail("dump returned no data and didn't error")
        return s, False
    if all_ff(data):
        s.succeed("dump returned all 0xFF (debug-blocked read; expected)")
        return s, True
    # Anything else means the chip handed real bytes back through the
    # debug interface — RDP-1 is not actually in effect.
    s.fail("dump returned real bytes; RDP-1 does not appear to be enforced")
    return s, False


def step_bootloader_runs(hla_serial: str, timeout: int) -> tuple[Step, bool]:
    s = Step("step_4_bootloader_still_boots")
    try:
        import serial  # noqa: F401
    except ImportError:
        s.fail("pyserial not installed")
        return s, False

    port = hil.find_serial_port(hla_serial=hla_serial)
    if not port:
        s.fail("could not find serial port for chip")
        return s, False

    import serial
    try:
        ser = serial.Serial(port, 115200, timeout=2)
    except Exception as e:
        s.fail(f"serial open failed: {e}")
        return s, False

    try:
        ser.reset_input_buffer()
        # Force a reset so we capture the bootloader banner from the start.
        run_openocd(hla_serial, "reset run", timeout=10)
        deadline = time.time() + timeout
        saw_banner = False
        while time.time() < deadline:
            if ser.in_waiting:
                line = ser.readline().decode("utf-8", errors="ignore").rstrip()
                if line:
                    print(f"  {line}")
                    if BOOTLOADER_BANNER_RE.search(line):
                        saw_banner = True
                        break
            else:
                time.sleep(0.05)
        if saw_banner:
            s.succeed("bootloader UART banner observed")
            return s, True
        s.fail(f"no bootloader banner within {timeout}s")
        return s, False
    finally:
        try: ser.close()
        except Exception: pass


def step_regress_to_l0(hla_serial: str) -> tuple[Step, bool]:
    s = Step("step_5_regress_to_l0_mass_erase")
    rc = subprocess.run(
        ["python3", "scripts/set_rdp.py",
         "--hla-serial", hla_serial, "--level", "0", "--confirm"],
        cwd=project_root(), capture_output=True, text=True, timeout=120,
    )
    text = rc.stdout + rc.stderr
    print(text)
    if rc.returncode != 0:
        s.fail(f"set_rdp.py --level 0 returned {rc.returncode}")
        return s, False
    if "RDP level is now 0" not in text:
        s.fail("set_rdp.py did not confirm L0 readback after regression")
        return s, False

    # Sanity: sector 0 should read as all-FF (mass-erase wiped it).
    succeeded, data, msg = dump_sector0(hla_serial)
    if not succeeded:
        s.fail(f"after L1→L0, dump_image still refuses: {msg.strip()}")
        return s, False
    if data is None or not all_ff(data):
        s.fail("after L1→L0, sector 0 is not all-FF — mass erase incomplete")
        return s, False
    s.succeed("mass-erase complete; sector 0 reads 0xFF; RDP back at L0")
    return s, True


def step_recovery_reflash(hla_serial: str) -> tuple[Step, bool]:
    s = Step("step_6_reflash_bootloader_and_app")
    bootloader_elf = (project_root() / "build" / "apps" / "bootloader"
                      / "loader" / "loader.elf")
    app_signed = (project_root() / "build" / "apps" / "bootloader"
                  / "app_blinky_signed" / "app_blinky_signed.signed.bin")

    if not bootloader_elf.exists() or not app_signed.exists():
        s.fail(
            f"missing build artifacts (bootloader_elf={bootloader_elf.exists()}, "
            f"app_signed={app_signed.exists()}). "
            "Run `make EXAMPLE=bootloader && make EXAMPLE=app_blinky_signed` "
            "before --skip-build."
        )
        return s, False

    rc, stdout, stderr = run_openocd(
        hla_serial,
        f"program {bootloader_elf} verify",
        f"program {app_signed} {SLOT_A_BASE:#010x} verify",
        "reset run",
        timeout=60,
    )
    if rc != 0:
        s.fail(f"openocd reflash failed (exit {rc}): "
               f"{(stderr or stdout)[-400:].strip()}")
        return s, False
    s.succeed("bootloader + slot-A reflashed")
    return s, True


# -------------------- JUnit XML --------------------

def write_junit(path: str, steps: list[Step]) -> None:
    suites = Element("testsuites")
    failures = sum(1 for s in steps if s.ok is False)
    errors = sum(1 for s in steps if s.ok is None)
    suite = SubElement(
        suites, "testsuite",
        name="RDP HIL (Phase 1.10)",
        tests=str(len(steps)),
        failures=str(failures),
        errors=str(errors),
        time="0",
    )
    for s in steps:
        tc = SubElement(suite, "testcase",
                        classname=s.classname, name=s.name, time="0")
        if s.ok is False:
            SubElement(tc, "failure", message=s.message).text = s.message
        elif s.ok is None:
            SubElement(tc, "error", message=s.message or "skipped").text = (
                s.message or "skipped"
            )
        elif s.message:
            SubElement(tc, "system-out").text = s.message
    indent(suites)
    ElementTree(suites).write(path, encoding="unicode", xml_declaration=True)


# -------------------- main --------------------

def main(argv: list[str] | None = None) -> int:
    try:
        sys.stdout.reconfigure(line_buffering=True)
    except Exception:
        pass

    parser = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--board", choices=list(hil.BOARD_REGISTRY.keys()),
        default="dev", help='Board role.  Defaults to "dev".  "ci" is rejected.')
    parser.add_argument("--hla-serial", default="",
        help="ST-LINK serial number (overrides --board).")
    parser.add_argument("--timeout", type=int, default=10,
        help="UART boot-banner timeout per pass, seconds.")
    parser.add_argument("--allow-ci-serial", action="store_true",
        help="Override the CI-serial guard.  Don't use this on the CI rig.")
    parser.add_argument("--junit-xml", default="rdp-test-results.xml")
    args = parser.parse_args(argv)

    if os.environ.get("STM32_BARE_METAL_CI") == "1":
        hil.log_error(
            "STM32_BARE_METAL_CI=1 — refusing to run the RDP test under the CI guard."
        )
        return 2

    hla_serial = (
        hil.BOARD_REGISTRY[args.board] if args.board else args.hla_serial
    )

    if hla_serial == hil.BOARD_REGISTRY.get("ci") and not args.allow_ci_serial:
        hil.log_error(
            f"Refusing to run the RDP test against the CI board "
            f"(ST-LINK serial {hla_serial}).  Pass --board dev or use a "
            f"different --hla-serial."
        )
        return 2

    steps: list[Step] = []

    # Step 1
    s, ok = step_read_l0(hla_serial)
    steps.append(s)
    if not ok:
        write_junit(args.junit_xml, steps)
        return 2

    # Step 2
    s, ok = step_engage_l1(hla_serial)
    steps.append(s)
    if not ok:
        write_junit(args.junit_xml, steps)
        return 1

    # Step 3
    s, ok = step_read_blocked(hla_serial)
    steps.append(s)
    if not ok:
        # Try to recover the chip even if the assertion failed.
        hil.log_warning("Attempting L0 regression for safety...")
        recover = subprocess.run(
            ["python3", "scripts/set_rdp.py",
             "--hla-serial", hla_serial, "--level", "0", "--confirm"],
            cwd=project_root(), capture_output=True, text=True, timeout=120,
        )
        sys.stderr.write(recover.stdout + recover.stderr)
        write_junit(args.junit_xml, steps)
        return 1

    # Step 4
    s, ok = step_bootloader_runs(hla_serial, args.timeout)
    steps.append(s)
    # Continue to step 5 either way — we still want to regress to L0.

    # Step 5
    s5, ok5 = step_regress_to_l0(hla_serial)
    steps.append(s5)

    # Step 6 — recovery reflash, run regardless so the rig comes home.
    s6, ok6 = step_recovery_reflash(hla_serial)
    steps.append(s6)

    write_junit(args.junit_xml, steps)
    return 0 if all(s.ok is True for s in steps) else 1


if __name__ == "__main__":
    sys.exit(main())
