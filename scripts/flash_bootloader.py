#!/usr/bin/env python3
"""One-shot bootloader flasher for the stm32-bare-metal NUCLEO setup.

Phase 1.5 (#151) reserves sector 0 for the bootloader.  Every other app
is linked at slot A (0x08010000) and must be loaded *through* the
bootloader.  A board with a blank or out-of-date sector 0 cannot run
any of the slot-A apps, so each NUCLEO needs the bootloader programmed
once before it joins the workflow.

This script wraps the steps so a fresh checkout can prep a board with
a single command:

  python3 scripts/flash_bootloader.py             # auto-detect probe
  python3 scripts/flash_bootloader.py --board dev # pin to dev rig
  python3 scripts/flash_bootloader.py --hla-serial 0660FF...

It builds `make EXAMPLE=bootloader` (skip with `--skip-build`), invokes
OpenOCD with the right `adapter serial` argument, and then reads back
the first two words of flash to confirm the bootloader's vector table
is in place.

Exit codes:
    0  bootloader programmed and verified
    1  flash failed or post-flash readback didn't look like a bootloader
    2  build / openocd error

Refuses to run when the HIL CI environment variable is set
(`STM32_BARE_METAL_CI=1`) — the CI runner must never reprogram sector 0.
"""

import argparse
import os
import re
import subprocess
import sys
import time
from pathlib import Path

# Re-use the board registry from run_hil_tests.py so there is a single source
# of truth for which ST-LINK serial belongs to which role.
sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_hil_tests as hil  # noqa: E402
import boards  # noqa: E402

OPENOCD_CFG = "board/st_nucleo_f4.cfg"
BOOTLOADER_BASE = 0x08000000


def project_root() -> Path:
    return Path(__file__).resolve().parent.parent


def build_bootloader() -> Path | None:
    hil.log_info("Building bootloader...")
    try:
        subprocess.run(
            ["make", "EXAMPLE=bootloader"],
            cwd=project_root(),
            check=True,
            timeout=180,
        )
    except subprocess.CalledProcessError as e:
        hil.log_error(f"make EXAMPLE=bootloader failed (exit {e.returncode})")
        return None
    except subprocess.TimeoutExpired:
        hil.log_error("Bootloader build timed out")
        return None

    elf = (project_root() / "build" / "apps" / "bootloader" / "loader"
           / "loader.elf")
    if not elf.exists():
        hil.log_error(f"Build completed but {elf} not found")
        return None
    hil.log_success(f"Built {elf.relative_to(project_root())}")
    return elf


def flash(elf_path: Path, hla_serial: str) -> bool:
    hil.log_info(f"Programming sector 0 with {elf_path.name}...")
    cmd = ["openocd"]
    if hla_serial:
        cmd += ["-c", f"adapter serial {hla_serial}"]
        hil.log_info(f"Pinned to ST-LINK serial: {hla_serial}")
    cmd += [
        "-f", OPENOCD_CFG,
        "-c", f"program {elf_path} verify reset exit",
    ]
    try:
        result = subprocess.run(
            cmd, cwd=project_root(), capture_output=True, text=True,
            check=True, timeout=60,
        )
    except subprocess.CalledProcessError as e:
        hil.log_error(f"openocd failed (exit {e.returncode})")
        sys.stderr.write(e.stderr or "")
        return False
    except subprocess.TimeoutExpired:
        hil.log_error("openocd flash timed out after 60 s")
        return False

    # OpenOCD prints **Verified OK** on a successful program+verify.
    if "Verified OK" not in (result.stdout + result.stderr):
        hil.log_error("openocd did not print 'Verified OK' — flash may not have succeeded")
        sys.stderr.write(result.stderr or "")
        return False
    hil.log_success("Flash + verify OK")
    return True


def readback_sector0(hla_serial: str) -> tuple[int, int] | None:
    """Halt the chip and read the first two words of flash.

    A working bootloader image must have:
      [0] initial MSP — top of the 128 KB SRAM, i.e. 0x20020000
      [1] reset vector — anywhere inside sector 0 (0x08000000..0x08003FFF)
                         with the Thumb bit set.

    Returns (msp, reset) on success, or None on read failure.
    """
    cmd = ["openocd"]
    if hla_serial:
        cmd += ["-c", f"adapter serial {hla_serial}"]
    cmd += [
        "-f", OPENOCD_CFG,
        "-c", "init",
        "-c", "halt",
        "-c", f"mdw {BOOTLOADER_BASE:#010x} 2",
        "-c", "exit",
    ]
    try:
        result = subprocess.run(
            cmd, cwd=project_root(), capture_output=True, text=True,
            check=True, timeout=20,
        )
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as e:
        hil.log_error(f"openocd readback failed: {e}")
        return None

    # Output line of interest looks like:
    #   0x08000000: 20020000 080004f5
    pattern = re.compile(
        rf"^{BOOTLOADER_BASE:#010x}: ([0-9a-fA-F]{{8}}) ([0-9a-fA-F]{{8}})",
        re.MULTILINE,
    )
    match = pattern.search(result.stdout + result.stderr)
    if not match:
        hil.log_error("Couldn't parse mdw output — is the chip connected?")
        return None
    msp = int(match.group(1), 16)
    reset = int(match.group(2), 16)
    return msp, reset


def looks_like_bootloader(msp: int, reset: int) -> bool:
    """Sanity-check the readback values match a freshly-flashed bootloader."""
    if msp != 0x20020000:
        hil.log_error(
            f"Initial MSP at 0x08000000 = {msp:#010x}, expected 0x20020000 "
            "(top of 128 KB SRAM). Sector 0 may not contain a valid image.")
        return False
    if not (BOOTLOADER_BASE <= reset < BOOTLOADER_BASE + 16 * 1024):
        hil.log_error(
            f"Reset vector at 0x08000004 = {reset:#010x}, expected somewhere in "
            f"[{BOOTLOADER_BASE:#010x}, {BOOTLOADER_BASE + 16 * 1024:#010x}). "
            "Sector 0 may not contain a valid image.")
        return False
    if (reset & 1) == 0:
        hil.log_error(
            f"Reset vector {reset:#010x} has the Thumb bit clear — "
            "the chip would HardFault before running the first instruction.")
        return False
    return True


def main(argv: list[str] | None = None) -> int:
    if os.environ.get("STM32_BARE_METAL_CI") == "1":
        hil.log_error("STM32_BARE_METAL_CI=1 — refusing to reflash sector 0 from CI.")
        return 2

    parser = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--board", choices=list(boards.BOARD_REGISTRY.keys()),
        help="Pin to a registered board role (resolves to its ST-LINK serial).")
    parser.add_argument("--hla-serial", default="",
        help=("ST-LINK serial number to target. Pass an empty string (the "
              "default) to let openocd pick whatever it finds. Overridden by "
              "--board when both are given."))
    parser.add_argument("--skip-build", action="store_true",
        help="Skip `make EXAMPLE=bootloader` (use the existing build).")
    parser.add_argument("--skip-verify", action="store_true",
        help="Skip the post-flash readback sanity check.")
    args = parser.parse_args(argv)

    hla_serial = (
        boards.BOARD_REGISTRY[args.board] if args.board else args.hla_serial
    )

    if args.skip_build:
        elf = (project_root() / "build" / "apps" / "bootloader" / "loader"
               / "loader.elf")
        if not elf.exists():
            hil.log_error(f"--skip-build was passed but {elf} doesn't exist")
            return 2
    else:
        elf = build_bootloader()
        if not elf:
            return 2

    if not flash(elf, hla_serial):
        return 1

    if args.skip_verify:
        return 0

    # Give the chip a moment after the post-flash reset before re-attaching.
    time.sleep(0.5)

    readback = readback_sector0(hla_serial)
    if not readback:
        return 1
    msp, reset = readback
    hil.log_info(f"Sector 0 readback: MSP={msp:#010x}, Reset={reset:#010x}")
    if not looks_like_bootloader(msp, reset):
        return 1
    hil.log_success("Bootloader readback looks valid. Board is ready for slot-A apps.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
