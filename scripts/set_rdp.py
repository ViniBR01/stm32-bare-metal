#!/usr/bin/env python3
"""Read or set STM32F411 readout-protection (RDP) level via OpenOCD.

Plan 001 Phase 1.10 (#169).  See
docs/wiki/plans/001-bootloader/rdp.md for the full background, threat
model, and recovery procedure.

Three operations:

    python3 scripts/set_rdp.py --status                 # read-only
    python3 scripts/set_rdp.py --level 1 --confirm      # arm RDP-1
    python3 scripts/set_rdp.py --level 0 --confirm      # mass-erase, back to L0
    RDP_L2_BURN_BOARD=1 python3 scripts/set_rdp.py --level 2 --confirm
                                                        # PERMANENT — no recovery

Refusal rules:

  * --level requires --confirm.  Running with neither writes nothing.
  * --level 2 additionally requires the env var RDP_L2_BURN_BOARD=1.
    The README and rdp.md document why.
  * STM32_BARE_METAL_CI=1 disables every write path.  The HIL CI runner
    sets this; honoring it stops a typo in a workflow file from putting
    the CI board into RDP-1.
  * --board ci (or an --hla-serial that resolves to the CI board)
    causes any write attempt to abort.

Implementation note: the option-byte programming is delegated to
OpenOCD's `stm32f2x options_write` command, which the F4 target
inherits.  We don't hand-craft OPTKEYR/OPTCR writes here — OpenOCD
already does the unlock/program/lock dance correctly and atomically.
"""

import argparse
import os
import re
import subprocess
import sys
import time
from pathlib import Path

# Re-use the board registry from run_hil_tests so all three scripts
# (run_hil_tests, flash_bootloader, set_rdp) agree on which ST-LINK
# serial belongs to which role.
sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_hil_tests as hil  # noqa: E402
import boards  # noqa: E402

OPENOCD_CFG = "board/st_nucleo_f4.cfg"

# The valid STM32F4 RDP byte values per RM0383 §3.7:
#   0xAA -> Level 0
#   0xCC -> Level 2
#   any other byte -> Level 1.  We pick 0xBB as the canonical L1 byte
#   because it differs from 0xFF (the post-erase value), so a partial
#   programming hiccup is more easily distinguished from "intermediate".
RDP_BYTE = {0: 0xAA, 1: 0xBB, 2: 0xCC}

# Mass-erase budget: RM0383 quotes ~16 s typical for a full chip erase
# at 2.7 V.  Allow plenty of margin around the OpenOCD overhead.
L1_TO_L0_TIMEOUT_S = 60


def project_root() -> Path:
    return Path(__file__).resolve().parent.parent


def run_openocd(hla_serial: str, *commands: str,
                timeout: int = 30) -> tuple[int, str, str]:
    """Run `openocd -c init -c reset halt -c <cmds...> -c exit`.

    Returns (returncode, stdout, stderr).  Does not raise on non-zero
    exit; the caller decides whether the command is allowed to fail
    (e.g. a `dump_image` against an RDP-1 chip is *expected* to fail).
    """
    cmd = ["openocd"]
    if hla_serial:
        cmd += ["-c", f"adapter serial {hla_serial}"]
    cmd += ["-f", OPENOCD_CFG,
            "-c", "init", "-c", "reset halt"]
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
        return 124, e.stdout or "", e.stderr or f"timeout after {timeout} s"


def read_options(hla_serial: str) -> dict | None:
    """Run `stm32f2x options_read 0` and parse the output.

    The relevant lines look like:
        Device Security Bit Set
        user_options = 0x...
        RDP level: 1 (0xBB)
    Different OpenOCD versions wrap the wording slightly differently.
    We extract the RDP level (0/1/2) and the raw user_options if found.
    """
    rc, stdout, stderr = run_openocd(
        hla_serial, "stm32f2x options_read 0", timeout=20,
    )
    text = stdout + "\n" + stderr
    if rc != 0 and "RDP" not in text:
        hil.log_error("openocd options_read failed:")
        sys.stderr.write(text)
        return None

    info: dict = {"raw": text}

    # Try a handful of OpenOCD output variations.
    m = re.search(r"RDP\s+(?:level|Level)\s*[:=]?\s*(\d)", text)
    if m:
        info["level"] = int(m.group(1))
    elif "Device Security Bit Set" in text:
        # Older OpenOCD: presence of the security bit ≈ RDP-1 (or L2,
        # but L2 chips don't talk to OpenOCD at all so we'd have failed
        # earlier).
        info["level"] = 1
    elif "RDP" in text and ("0xAA" in text or "(0xAA)" in text):
        info["level"] = 0
    elif rc == 0:
        # No security bit, no failure: must be Level 0.
        info["level"] = 0

    m = re.search(r"user_options\s*[:=]\s*(0x[0-9a-fA-F]+)", text)
    if m:
        info["user_options"] = int(m.group(1), 16)

    if "level" not in info:
        hil.log_error("Could not parse RDP level from options_read output:")
        sys.stderr.write(text)
        return None

    return info


def cmd_status(hla_serial: str) -> int:
    info = read_options(hla_serial)
    if info is None:
        return 1
    level = info["level"]
    user_options = info.get("user_options")
    hil.log_info(f"RDP level: {level}")
    if user_options is not None:
        hil.log_info(f"user_options: {user_options:#010x}")
    if level == 0:
        hil.log_info("Debug fully open; user flash readable. (factory state)")
    elif level == 1:
        hil.log_info("Debug-attached read/program of user flash blocked.")
        hil.log_info("Regression to L0 will MASS-ERASE user flash.")
    elif level == 2:
        hil.log_warning("Level 2 — debug permanently disabled. "
                        "If you see this in --status, OpenOCD probably "
                        "couldn't even attach.  Double-check the chip.")
    return 0


def write_rdp(hla_serial: str, level: int) -> bool:
    """Drive `stm32f2x options_write` to set the new RDP byte.

    The OpenOCD command takes the bank, the user-options bits, and the
    RDP byte separately.  We reuse the chip's current user_options
    bits so we don't accidentally clobber WRP / BOR / WDG_SW choices.
    """
    if level not in RDP_BYTE:
        raise ValueError(f"unsupported RDP level: {level}")

    info = read_options(hla_serial)
    if info is None:
        hil.log_error("Refusing to write: could not read current options.")
        return False
    current_level = info["level"]
    user_options = info.get("user_options")

    hil.log_info(f"Current RDP level: {current_level}")

    if current_level == 2:
        hil.log_error("Chip is at Level 2.  Permanent and unrecoverable. "
                      "Cannot transition.")
        return False

    if current_level == level:
        hil.log_warning(f"Chip already at RDP level {level}; nothing to do.")
        return True

    # Reuse the existing user_options bits if we have them.  If we
    # don't, fall back to OpenOCD's default of 0xFFEC, which is what
    # `stm32f2x options_write 0 0xFFEC <RDP>` documents as "no WRP, BOR
    # off, watchdog HW".  Applying that to a chip that had WRP set
    # would clear write protection, which is acceptable for a dev
    # board.
    if user_options is None:
        hil.log_warning("user_options unknown; falling back to OpenOCD "
                        "default 0xFFEC.")
        user_options = 0xFFEC

    rdp_byte = RDP_BYTE[level]
    hil.log_info(
        f"Writing options: user_options={user_options:#06x}, "
        f"RDP byte={rdp_byte:#04x} (level {level})"
    )

    # L1 -> L0 triggers a mass erase of user flash inside the chip.
    # Bump the OpenOCD timeout accordingly.
    op_timeout = L1_TO_L0_TIMEOUT_S if (current_level == 1 and level == 0) else 30

    rc, stdout, stderr = run_openocd(
        hla_serial,
        f"stm32f2x options_write 0 {user_options:#06x} {rdp_byte:#04x}",
        timeout=op_timeout,
    )
    text = stdout + "\n" + stderr
    if rc != 0:
        hil.log_error(f"openocd options_write failed (exit {rc}):")
        sys.stderr.write(text)
        return False

    # Some OpenOCD versions print a noisy success banner; just check
    # that no obvious error keyword shows up.
    if "Error:" in text and "Programming Failed" in text:
        hil.log_error("OpenOCD reported a programming failure:")
        sys.stderr.write(text)
        return False

    if current_level == 1 and level == 0:
        hil.log_info("L1 → L0 issued.  Waiting for in-chip mass erase to settle...")
        # The options_write call already polled FLASH_SR.BSY; this is
        # belt-and-braces in case OpenOCD returned early for an
        # implementation that re-runs reset before the erase finishes.
        time.sleep(2.0)

    # Power cycle isn't required — OpenOCD has issued a system reset
    # — but we need to halt the chip again before re-reading options
    # so the next `init/reset halt` doesn't race.
    after = read_options(hla_serial)
    if after is None:
        hil.log_warning(
            "Could not read options after write — the chip may be at L1 now, "
            "which can confuse some OpenOCD versions during a reattach.  "
            "Power-cycle the board and run --status to confirm."
        )
        # Treat as a soft success: the write itself didn't fail above.
        return True

    if after["level"] == level:
        hil.log_success(f"RDP level is now {level}.")
        return True
    hil.log_error(
        f"After write, RDP level is {after['level']} but {level} was requested."
    )
    return False


def is_ci_serial(hla_serial: str) -> bool:
    return hla_serial == boards.BOARD_REGISTRY.get("ci")


def main(argv: list[str] | None = None) -> int:
    if os.environ.get("STM32_BARE_METAL_CI") == "1":
        # Allow --status to run; refuse anything that writes.  This
        # mirrors the spirit of flash_bootloader.py's guard.
        # We have to parse args before deciding, but the side effect is
        # only print + exit, so that's fine.
        pass

    parser = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--board", choices=list(boards.BOARD_REGISTRY.keys()),
        help='Board role: "ci" or "dev".  Resolves to the matching '
             'ST-LINK serial.  Refuses to write to the CI board.')
    parser.add_argument("--hla-serial", default="",
        help="ST-LINK serial number to target.  Empty (default) lets "
             "openocd pick whatever it finds.  Overridden by --board.")
    parser.add_argument("--status", action="store_true",
        help="Read and print the chip's current RDP level + option bytes.")
    parser.add_argument("--level", type=int, choices=[0, 1, 2],
        help="Set RDP to this level.  Requires --confirm.")
    parser.add_argument("--confirm", action="store_true",
        help="Required to actually perform a write.  Without it, "
             "--level prints the intended action and exits.")
    args = parser.parse_args(argv)

    hla_serial = (
        boards.BOARD_REGISTRY[args.board] if args.board else args.hla_serial
    )

    # Default action with no flags is --status.
    if not args.status and args.level is None:
        return cmd_status(hla_serial)

    if args.status:
        return cmd_status(hla_serial)

    # ---- Write path from here on ----
    if args.level is None:
        return cmd_status(hla_serial)

    if not args.confirm:
        hil.log_error("--level requires --confirm to perform a write.")
        hil.log_info(f"Would have set RDP level to {args.level}.")
        return 2

    if os.environ.get("STM32_BARE_METAL_CI") == "1":
        hil.log_error(
            "STM32_BARE_METAL_CI=1 — refusing to write option bytes from CI."
        )
        return 2

    if is_ci_serial(hla_serial):
        hil.log_error(
            f"Refusing to write option bytes to the CI board "
            f"(ST-LINK serial {hla_serial}).  Use --board dev or another "
            f"--hla-serial."
        )
        return 2

    if args.level == 2 and os.environ.get("RDP_L2_BURN_BOARD") != "1":
        hil.log_error(
            "RDP Level 2 is PERMANENT.  Refusing to proceed without "
            "RDP_L2_BURN_BOARD=1 in the environment."
        )
        hil.log_error("See docs/wiki/plans/001-bootloader/rdp.md.")
        return 2

    if args.level == 2:
        hil.log_warning("=" * 60)
        hil.log_warning(
            "About to set RDP Level 2.  This is PERMANENT.  The chip "
            "cannot be debugged or re-flashed via JTAG/SWD afterwards."
        )
        hil.log_warning("=" * 60)

    return 0 if write_rdp(hla_serial, args.level) else 1


if __name__ == "__main__":
    sys.exit(main())
