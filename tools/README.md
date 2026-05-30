# tools/

Host-side utilities that operate on or alongside built firmware artifacts.

## What goes here

Programs (typically Python) that a builder or operator runs on a host
machine — never on the target.

| Tool | Purpose | Track |
|---|---|---|
| `keygen.py` | Generate ECDSA P-256 keypair; emit `bootloader_pubkey.c` | Plan 001 |
| `sign_image.py` | Sign a firmware payload, produce a flashable `.signed.bin` | Plan 001 |
| `_img_format.py` | Shared on-flash format spec (struct, CRC, magics) imported by the other Plan 001 tools | Plan 001 |

Planned for future tracks:

- `ota_send.py` — push a signed image over UART/SPI to a running bootloader (Plan 001).
- `partition_dump.py` — read flash via OpenOCD and pretty-print slot metadata (Plan 001).
- `ber_plot.py` — replay a captured BPSK demodulator log and plot bit-error-rate curves (Plan 002).

For Plan 001 signing, see [docs/wiki/plans/001-bootloader/signing.md](../docs/wiki/plans/001-bootloader/signing.md).

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
