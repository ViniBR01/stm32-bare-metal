# lib/

Middleware libraries — code that sits above drivers but below applications.

## What goes here

Self-contained libraries that:

- Have **no `main()`** — they don't own a firmware image.
- Have **no direct hardware ownership** — peripheral access goes through
  `drivers/` (e.g. crypto pulls entropy from `drivers/rng`, not directly from
  the RNG registers).
- Are **reusable across multiple apps** — typically across multiple plans.

Examples planned for future tracks:

- `lib/crypto/` — ECDSA verification, SHA-256, HKDF (Plan 001).
- `lib/framing/` — COBS / SLIP / minimal HDLC framing for inter-board comms (Plan 002).
- `lib/modem/` — software BPSK modulator, demodulator, FEC (Plan 002).

## What does NOT go here

- Drivers (peripheral register code) — `drivers/`.
- Leaf utilities like `printf_dma`, `cli`, `string_utils` — `utils/`.
- Standalone host tools — `tools/`.
- Firmware applications — `apps/`.

## Layout convention

Each library lives in its own subdirectory and follows the same shape as
`drivers/` and `utils/`:

```
lib/<name>/
├── Makefile        # mirrors drivers/Makefile
├── inc/<name>.h    # public header(s)
└── src/<name>.c    # implementation
```

Each lib builds to `$(BUILD_DIR)/lib/<name>/lib<name>.a`. Apps that need the
library link it explicitly via `<APP>_DEPS` in `apps/<group>/Makefile` and
declare the include path with `-I$(LIB_DIR)/<name>/inc`.

Host tests live in `tests/lib/<name>/` and follow the existing Unity pattern
under `tests/string_utils/`.

## Skeleton

A minimal `lib/skeleton/` is included to prove the build plumbing end-to-end.
It exports a single function and is covered by one host unit test in
`tests/lib/skeleton/`. Real libraries land with later plans; the skeleton
stays as documentation-by-example until at least one real lib exists, and may
be removed later.
