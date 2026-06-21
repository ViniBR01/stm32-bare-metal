#!/usr/bin/env python3
"""Erase metadata sectors on the NUCLEO board for a clean-slate boot.

The Phase 1.9 bootloader computes a rollback floor from the highest
monotonic_counter across both slot-metadata sectors.  If a prior test
run left elevated counters (e.g. anti-rollback test crashed mid-run),
subsequent tests that flash IMAGE_VERSION=1 images will be rejected.

This script erases sectors 1 and 2 (slot A and slot B metadata) so the
bootloader sees floor=0 on the next boot.  It is called at the start
of the CI HIL job and can be used manually before any dev-board session.

Exit codes:
    0  metadata erased successfully
    1  OpenOCD failed
"""

import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_hil_tests as hil  # noqa: E402
import boards  # noqa: E402


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--board", choices=list(boards.BOARD_REGISTRY.keys()),
                   help='Board role: "ci" or "dev".')
    p.add_argument("--hla-serial", default=boards.DEFAULT_HLA_SERIAL,
                   help="Explicit ST-LINK serial (overrides --board).")
    args = p.parse_args()

    hla_serial = (boards.BOARD_REGISTRY[args.board] if args.board
                  else args.hla_serial)

    hil.log_info("Erasing metadata sectors 1-2 (slot A + slot B metadata)...")
    try:
        hil.openocd_run(hla_serial,
                        "flash erase_sector 0 1 1",
                        "flash erase_sector 0 2 2")
    except RuntimeError as e:
        hil.log_error(str(e))
        return 1

    hil.log_success("Metadata sectors erased — board ready for clean boot")
    return 0


if __name__ == "__main__":
    sys.exit(main())
