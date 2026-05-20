# tools/

Host-side utilities that operate on or alongside built firmware artifacts.

## What goes here

Programs (typically Python) that a builder or operator runs on a host
machine — never on the target. Examples planned for future tracks:

- `tools/sign_image.py` — sign a firmware image with an ECDSA private key (Plan 001).
- `tools/ota_send.py` — push a signed image over UART/SPI to a running bootloader (Plan 001).
- `tools/ber_plot.py` — replay a captured BPSK demodulator log and plot bit-error-rate curves (Plan 002).

## What does NOT go here

- Repo automation (CI helpers, HIL test runner, worktree management) →
  `scripts/`. Those are about the repository itself.
- Firmware code (drivers, libs, apps) → `drivers/`, `lib/`, `apps/`.
- Host unit tests for firmware code → `tests/`.

The rule of thumb: if it runs on the host and consumes or produces a
firmware artifact, it goes in `tools/`. If it runs on the host to make
the repo work (build, test, deploy this codebase), it goes in `scripts/`.

## Convention

- Each tool is standalone and self-contained — no shared package.
- Document the tool's purpose, inputs, and outputs at the top of its file.
- Pure-Python preferred; if a tool has Python dependencies, list them in a
  comment block at the top of the file (no requirements.txt — keep it
  trivial).
- Not built by `make`. Invoked directly: `python3 tools/<name>.py ...`.
