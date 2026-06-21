#!/usr/bin/env python3
"""Board registry — the single source of truth for ST-LINK serials.

The data lives in ``boards.json`` next to this file; this module is a tiny,
stdlib-only loader so it can be imported cheaply from any script (without
dragging in the full HIL test runner) and read from the Makefile via
``python3 -c "import boards; ..."``.

A "board role" (``ci`` / ``dev``) maps to the ST-LINK serial number of the
NUCLEO that plays that role.  The serial is used both for OpenOCD probe
selection (``hla_serial`` / ``adapter serial``) and to derive the stable
``/dev/serial/by-id/`` symlink for the serial port.

Roles:
    ci   — the board wired to the GitHub Actions HIL runner.  CI owns it;
           manual/agent commands must never target it (see CLAUDE.md).
    dev  — the development board for manual flashing and agent HIL runs.

To register a new board or swap a physically-replaced one, edit
``boards.json`` only — every consumer reads through this loader.
"""

import json
from pathlib import Path

_BOARDS_JSON = Path(__file__).resolve().parent / "boards.json"


def _load() -> dict:
    with open(_BOARDS_JSON, "r", encoding="utf-8") as fh:
        return json.load(fh)


def load_registry() -> dict[str, str]:
    """Return the {role: st-link serial} mapping from boards.json."""
    return dict(_load()["boards"])


def roles() -> list[str]:
    """Return the list of known board roles (e.g. ['ci', 'dev'])."""
    return list(load_registry().keys())


def default_role() -> str:
    """Return the role used when none is specified (e.g. 'ci')."""
    return _load()["default_role"]


def default_serial() -> str:
    """Return the ST-LINK serial of the default role."""
    return load_registry()[default_role()]


def resolve_serial(role: str | None = None, hla_serial: str = "") -> str:
    """Resolve a board role (or explicit serial) to an ST-LINK serial.

    Mirrors the idiom used across the scripts: an explicit ``role`` wins and
    is looked up in the registry; otherwise ``hla_serial`` (which may be an
    empty string to mean "let OpenOCD auto-pick") is returned unchanged.
    """
    if role:
        return load_registry()[role]
    return hla_serial


# Module-level constants so consumers keep the familiar names.
BOARD_REGISTRY: dict[str, str] = load_registry()
DEFAULT_HLA_SERIAL: str = default_serial()


if __name__ == "__main__":
    import sys

    # Tiny CLI so shell/Make can query without writing inline Python:
    #   python3 scripts/boards.py serial dev   -> prints the dev serial
    #   python3 scripts/boards.py roles         -> prints "ci dev"
    args = sys.argv[1:]
    if args and args[0] == "roles":
        print(" ".join(roles()))
    elif args and args[0] == "serial":
        role = args[1] if len(args) > 1 else default_role()
        print(BOARD_REGISTRY.get(role, ""))
    else:
        print(json.dumps(BOARD_REGISTRY, indent=2))
