# Project Log

Chronological record of significant changes. Newest entries at the top.
Format: `## [YYYY-MM-DD] <type> | <title> (<PR/Issue>)`
Types: `merge`, `decision`, `milestone`, `infra`

## [2026-06-28] milestone | Wire RRC shaping into modem_sim: --shape flag + HIL Tier 9b (#207)

Follow-up to B0.4 (#196): the RRC core now runs inside the on-board modem demo
behind an opt-in flag, so the simulated transmission chain can produce real
oversampled waveforms end to end.

- `apps/dsp/modem_sim.c`: new `modem_run_chain_shaped()` —
  PRBS → BPSK → `rrc_tx_shape` (×SPS) → AWGN @ sample rate → `rrc_rx_match` →
  decimate at `k*SPS + rrc_chain_delay` → slice → compare, timed across seven
  stages (gen/mod/shape/channel/match/demod/check). A reference PRBS advanced
  once per decimated symbol keeps tx/checker aligned across the filter delay
  without a symbol FIFO. `modem run`/`modem sweep` gain a valueless `--shape`
  toggle (β=0.35, sps=4, span=8); without it the original one-sample-per-symbol
  path is byte-for-byte unchanged, preserving the B0.3 baselines.
- HIL **Tier 9b** in `apps/cli/test_harness.c`
  (`test_modem_bpsk_ber_awgn_shaped`): same PRBS9/seed1/6 dB/100k-bit run, RRC
  shaped. On-device gate = factor-2 BER band around theory AND cyc/bit under a
  (generous) shaped budget. Emits `modem_shaped_ber_snr6` (gated) plus seven
  report-only `modem_shaped_cyc_*` per-stage lines.
- `tests/baselines/performance.json`: added the `modem_shaped_*` keys with
  `null` cycles/ber_ppm — the runner reports them without gating until the first
  CI HIL capture populates them (same bootstrap as `modem_bpsk_ber_snr6`). The
  existing unshaped entries are untouched.
- Build: `modem_sim` links `$(DSP_LIB)`; `cli_simple` under `HIL_TEST=1` links
  it too. Host model (exact app algorithm) confirms noiseless BER=0 and
  theory-tracking BER (6 dB → ~2610 ppm vs 2388 theory).

## [2026-06-28] milestone | RRC pulse shaping + matched filter (q15 waveforms) (#196)

Plan 002 sub-track B0.4 — the modem moves from one-sample-per-symbol to real
oversampled waveforms. `lib/dsp` gains a root-raised-cosine FIR; the cascade of
TX shaping and the RX matched filter is a raised cosine, so the chain is
ISI-free at the Nyquist (symbol) instants.

- New `lib/dsp/{inc/rrc.h,src/rrc.c}` — `rrc_design(beta, sps, span)` builds a
  unit-energy (Σh²=1) q15 RRC into a self-contained instance; `rrc_push` is a
  streaming q15 FIR (64-bit accumulator, round+saturate); `rrc_tx_shape`
  upsamples symbols by zero-stuffing and filters; `rrc_rx_match` is the matched
  filter; `rrc_chain_delay` gives the symbol-instant decimation offset.
  Defaults: β=0.35, SPS=4, span=8 (33 taps). lib/dsp was header-only before, so
  it now builds `libdsp.a` (`DSP_LIB`) and is in `lib/Makefile` SUBDIRS.
- Unit energy is the design hinge: with ±1.0 symbols the matched-filter output
  at the symbol instant keeps Eb=1, so the existing AWGN channel and Eb/N0→σ
  mapping run unchanged at sample rate and BER still tracks the BPSK theory
  curve (the matched filter is information-lossless).
- Golden-reference host tests (`tests/lib/dsp/test_rrc.c`, 7 tests):
  C taps match the float64 Python reference (`vectors/gen_rrc_vectors.py` →
  `vectors/rrc_golden.h`) within ±2 LSB; cascade is ISI-free at symbol-spaced
  lags; full PRBS→shape→AWGN→match→decimate→slice chain gives BER=0 with no
  noise and tracks theory (0/2/4/6 dB) with noise.
- Scope is the `lib/dsp` DSP core + golden tests; the `modem_sim` firmware
  measurement chain and its calibrated HIL baselines (B0.3) are left untouched.

## [2026-06-26] milestone | Software BPSK modem on hardware: modem_sim CLI app + HIL Tier 9 (#195)

Plan 002 sub-track B0.3 — wired the merged software modem libs (`lib/prbs`,
`lib/modem`, `lib/channel`, `lib/dsp`) into firmware, the first time the q15
BPSK-over-AWGN chain runs on the Cortex-M4F.

- New interactive app `apps/dsp/modem_sim` (new `apps/dsp/` subdir): `modem run
  [--mod bpsk] [--snr <dB>] [--bits <N>]` prints measured BER vs closed-form
  theory and DWT cycles/bit; `modem sweep --snr <lo>:<hi>:<step>` prints an
  ASCII BER-vs-Eb/N0 table. Decimal SNR supported via a local `parse_float`
  (no `atof` in this codebase). Built/signed at slot A like `cli_simple`.
- New HIL **Tier 9** in `apps/cli/test_harness.c` (`test_modem_bpsk_ber_awgn`):
  PRBS9, seed 1, 6 dB, 100000 bits. Emits `TEST:modem_bpsk_ber_snr6:...:ber_ppm`.
  On-device gate = BER within a factor of two of theory AND cyc/bit under
  budget. Modem libs link into `cli_simple` only under `HIL_TEST=1`, keeping
  the production image free of the libm-heavy float path.
- `tests/baselines/performance.json` gained a `modem_bpsk_ber_snr6` entry with
  `null` cycles/ber_ppm — the runner reports the measured values without gating
  until populated from the first CI HIL run.
- Build plumbing: `PRBS_LIB`/`MODEM_LIB`/`CHANNEL_LIB` in `Makefile.common`,
  `dsp` added to `apps/Makefile` SUBDIRS, `modem_sim` to root `ALL_APPS`.

## [2026-06-20] infra | Max-clock SPI-DMA integrity made advisory (non-gating) (#185)

The `spi*_dma_psc2_256B` HIL smoke tests (SPI DMA at the fastest prescaler)
flake on a single corrupted byte because they run over a bench loopback jumper
that is electrically marginal at that clock. Cross-board measurement showed the
same firmware giving `integrity 5/5` on the CI board and `0/5` on the dev board,
flipping over time — a wiring artifact, not a regression (polled mode and all
lower prescalers stay clean).

`scripts/run_hil_tests.py` now treats integrity for `spi\d+_dma_psc2_*` as
**advisory**: logged loudly and emitted as a `skipped` JUnit case, but it no
longer fails the run or aborts the remaining HIL suites. Cycles/throughput for
those tests stay gated, and integrity for polled mode + prescalers >= 4 remains
a hard gate. Verified on hardware: the dev board (integrity 0/5) now exits 0;
the CI board (5/5) is unaffected. The hardware fix stays tracked in #185.

## [2026-06-20] infra | Board registry extracted; flash/serial/debug slot- and board-aware (#177)

Extracted the board registry out of `scripts/run_hil_tests.py` into a standalone
source of truth — `scripts/boards.json` (data) + `scripts/boards.py` (a tiny
stdlib-only loader exposing `BOARD_REGISTRY`, `DEFAULT_HLA_SERIAL`, `roles()`,
`resolve_serial()`). All HIL scripts now read the registry from `boards`;
`run_hil_tests.py` re-exports `BOARD_REGISTRY`/`DEFAULT_HLA_SERIAL` for
backward compatibility. Serials are unchanged, so the RDP/CI-serial safety
guards keep working.

- The root `Makefile` gained shared `BOARD` (default `dev`), `HLA_SERIAL`,
  resolving to `STLINK_SERIAL` via `boards.py`. `make flash`, `make serial`,
  and `make debug` now honor them (previously only `flash-bootloader` did).
- `make flash` flashes the profile's final image (`.signed.bin` for bootloader
  profiles, raw `.bin` for standalone) at `SLOT_BASE` and pins the ST-LINK
  probe — fixing the prior behavior of programming the `.elf` with no probe
  pinning.
- `make serial` resolves the board's `/dev/serial/by-id` port via
  `run_hil_tests.find_serial_port` (glob fallback preserved for macOS).
  `make debug` / `debug.sh` forward the probe serial (`adapter serial`).
- To register or swap a board, edit `scripts/boards.json` only. Updated
  `README.md`, `make help`, and `hil-remote-access.md`.

## [2026-06-20] infra | App target profiles — `PROFILE=` memory-map selector (#167)

Centralized the memory-map / signing choice into a single `PROFILE=` knob in
`Makefile.common`. A profile bundles linker script, `SLOT_BASE`,
`PROFILE_SUFFIX`, and `SIGN_IMAGE`. Two profiles ship: `bootloader` (default —
slot A/B, signed, `app_ls.ld`) and `standalone` (full-flash `0x08000000`,
unsigned, `stm32_ls.ld`). Apps stay profile-agnostic — they thread
`$(PROFILE_SUFFIX)` through output paths and gate signing on `$(SIGN_IMAGE)`.

- Closes #167: `apps/cli` and `apps/basic` are now slot-aware (previously
  `SLOT=B` silently clobbered the slot-A artifact). All app Makefiles
  (`cli`, `basic`, `app_blinky_signed`) build distinct A/B/standalone outputs.
- `make flash` / `make debug` are now `PROFILE`-aware (search
  `<app>$(PROFILE_SUFFIX).elf`).
- Every bootloader-profile slot now carries a descriptive suffix (`_a` / `_b`),
  so the slot-A path moved (`build/apps/cli/cli_simple_a/...`). The HIL /
  boot-smoke / OTA / verify / RDP / anti-rollback scripts that hard-coded the
  old no-suffix slot-A path were updated to the `_a` path in the same change.
- New **ADR 003 (`decisions/003-app-target-profiles.md`)** records the profile
  model, the per-slot position-dependence of bootloader images, and the
  deferred options (PIC / fixed exec region) for slot-agnostic images.
- `stm32_ls.ld` documented as the `standalone` profile script; updated
  `architecture.md`, `bootloader-skeleton.md`, `ab-slots.md`, `README.md`,
  `CLAUDE.md`, and `index.md`.

## [2026-06-20] milestone | Plan 001 complete — Phase 1.11 threat model + production gaps + ADR (#170)

Closes Plan 001.  Phases 1.5–1.10 shipped the bootloader, signing, OTA,
A/B slots, anti-rollback, and RDP-1.  This final phase documents what the
project defends against, what it doesn't, and what production would require.

New documentation:

- **`threat-model.md`** — four attacker classes (network, physical pre/post
  RDP, local user), seven defense mechanisms with source citations, six
  explicit non-goals, a summary table, and five trust assumptions.
- **`production-gap.md`** — eight gaps ordered by effort: signing-key
  custody, secure boot ROM (the biggest gap), TrustZone-M, anti-glitch,
  encrypted images, side-channel resistance, provisioning, audit trail.
- **ADR 002 (`decisions/002-image-format.md`)** — pins the `img_header_t`
  (140 B), `img_slot_metadata_t` (36 B), 512-byte payload alignment, flash
  sector layout, and ECDSA-P256 raw R||S signing format as frozen for the
  STM32F411RE deliverable.

Plan 001 row in roadmap.md flipped to "completed".

## [2026-06-04] milestone | Plan 001 Phase 1.9 — anti-rollback floor + fail_count writes (#168)

Closes Phase 1.9 in a single PR.  The bootloader now refuses to boot
any image whose `image_version` is below the highest version it has
ever booted, and the OTA receiver enforces the same check after a
successful verify.  `fail_count` writes (deferred from Phase 1.7)
land alongside the floor commit so the marginal cost of fail_count
is one extra write per boot.

New / changed components:

- **Pure helpers** in [lib/img/inc/img_header.h](../../lib/img/inc/img_header.h)
  — `img_header_meets_floor`, `img_compute_floor`,
  `img_compute_new_floor`, `img_fail_count_increment`/`tripped`,
  `img_pick_active_slot`, `img_slot_metadata_finalize`.  All
  host-testable; 25 new Unity tests in
  `tests/lib/img/test_img_header.c` cover the floor predicate, max
  helpers, fail_count clamp, slot-pick decision tree, and the
  finalize-then-parse round trip.
- **Bootloader** in
  [apps/bootloader/loader/main.c](../../apps/bootloader/loader/main.c)
  — full anti-rollback flow: read both metadata sectors, compute
  the floor, gate each slot attempt on `fail_count < MAX`, verify,
  then gate on `image_version >= floor`.  After picking a slot, a
  single `flash_slot_commit_metadata` writes `active=1`,
  `fail_count = clamped(prev+1)`, `monotonic_counter = max(image_version,
  floor, prev counter)` — one erase + program + readback per boot.
- **`verify_slot()`** now returns the parsed header so the floor
  check happens in one place without a second flash read.
- **OTA receiver** in
  [apps/bootloader/loader/ota.c](../../apps/bootloader/loader/ota.c)
  — post-verify floor check.  On rejection: bytes are written, but
  no metadata commit lands, and the previously-active slot stays
  active.  New `OTA_STATUS_ROLLBACK_REJECTED = 4` is reported on
  the wire (mirrored in `tools/_framing.py`).
- **`lib/bl_handshake/`** — new tiny library with one function,
  `bl_handshake_clear_fail_count()`.  Reads metadata, skips the
  commit if `fail_count` was already 0 (no flash wear in the
  steady-state clean-boot loop), otherwise resets it and re-commits.
  Wired into `apps/bootloader/app_blinky_signed/main.c` and
  `apps/cli/cli_simple.c` — both resolve the boot slot from
  `SCB->VTOR` so the same code covers slot-A and slot-B builds.
- **HIL `scripts/run_anti_rollback_test.py`** — four-pass flow:
  seed v1 in slot A, clean-upgrade OTA to v2 in slot B, downgrade
  OTA v1 rejected with `STATUS=rollback_rejected`, force-flashed
  downgrade rejected at boot with `BL: rollback ver=1 < floor=N`
  followed by fallback to slot B.  Wired into the standard CI
  `hil-tests` job after the existing OTA test.

Sector-0 budget after the cuts: **16352 / 16384 bytes** — ~32 bytes
of headroom.  `pick_active_slot` was lifted into `lib/img` to
eliminate the duplicated decision tree between `main.c` and
`ota.c`; that, plus tightening a couple of log strings, kept the
bootloader inside the cap.

Threat-model note: with Option 1 (floor in slot metadata), the
floor is wipeable via OpenOCD until Phase 1.10's RDP-1 closes the
debug-bus attack.  An attacker with debug access can already flash
arbitrary signed images, so the floor primarily defends the OTA
path — exactly the threat the per-image signature does not cover.

## [2026-06-03] milestone | Plan 001 Phase 1.10 — RDP-1 tooling + HIL test (#169)

Adds the readout-protection (RDP) option-byte story: docs, a host
script that toggles RDP levels via OpenOCD, and a manual-trigger HIL
test that cycles the dev board through L0 → L1 → L0 with assertions
at each step.

New components:

- **`docs/wiki/plans/001-bootloader/rdp.md`** — RM0383 §3.7/§3.8.1
  in plain English: L0/L1/L2 semantics, OPTKEYR unlock + OPTCR write
  sequence, threat-model fit (closes the OpenOCD-attaches-and-reads
  attack), explicit non-goals (glitching, side-channels, decapping,
  backup-domain registers), and the manual recovery procedure if an
  L1 → L0 mass-erase is interrupted.
- **`scripts/set_rdp.py`** — three-mode tool: `--status` reads the
  current RDP level (always safe); `--level {0,1,2} --confirm`
  drives `stm32f2x options_write` through OpenOCD. Refusal stack:
  `--level` requires `--confirm`; `--level 2` additionally requires
  `RDP_L2_BURN_BOARD=1`; `STM32_BARE_METAL_CI=1` and the CI
  ST-LINK serial both kill any write path. Reuses
  `run_hil_tests.BOARD_REGISTRY` so all three OpenOCD-driving
  scripts (`run_hil_tests`, `flash_bootloader`, `set_rdp`) agree on
  which serial is which board.
- **`scripts/run_rdp_test.py`** — six-step HIL flow: assert L0
  start → set L1 → assert `dump_image` is blocked or all-FF →
  confirm bootloader still UART-prints → regress to L0 (mass
  erase) → reflash bootloader + slot-A app from build artifacts so
  the rig comes home in a working state. Refuses to run on the CI
  board's ST-LINK serial; refuses to run with
  `STM32_BARE_METAL_CI=1`.
- **`.github/workflows/rdp-test.yml`** — manual `workflow_dispatch`
  trigger gated behind a typed-string confirmation input. Targets
  `--board dev`. Not part of the standard `hil-tests` job because
  the L1 → L0 mass erase would block every other HIL test that
  follows it on the same chip.

Cross-references updated: `bootloader-skeleton.md` and `ota.md` now
both flag that their OpenOCD-based recovery paths require an
L1 → L0 regression first under RDP-1. The plan page bumps Phase
1.10 to "in progress" and links to the new `rdp.md`.

Out of scope (deliberately): RDP Level 2 actuation, WRP sector
write-protect bits, PCROP. RDP-2 is documented but not driven by
any tooling — the gating env-var is meant to make a "let me try
this" mistake impossible.

## [2026-06-01] milestone | Plan 001 Phase 1.8 (part 2) — bootloader OTA receiver (#162)

Closes Phase 1.8.  Building on the framing layer from part 1
([#163](https://github.com/ViniBR01/stm32-bare-metal/pull/163)),
this drop wires up the end-to-end OTA path: app → bootloader →
host tool → bootloader → reset into the new image.

New components:

- **RTC backup driver** (`drivers/{inc,src}/rtc_backup.{h,c}`) —
  three-function helper: enable backup-domain writes, read DR0,
  write DR0.  ~30 LOC, used only by the OTA entry path.
- **`ota_request` CLI command** (`apps/cli/cli_commands.c`) —
  writes `RTC_BACKUP_OTA_MAGIC` (0x4F544131) into `RTC_BKP_DR0`
  and triggers `NVIC_SystemReset()`.  Backup register survives
  the reset, so the bootloader sees the magic at boot and enters
  OTA mode instead of normal verify-and-jump.
- **Bootloader OTA receiver** (`apps/bootloader/loader/ota.{h,c}`)
  — UART → framing decoder → state machine.  Drives
  `flash_slot_erase`, `flash_write_bytes`, the reused `verify_slot`
  path (lifted out of `main.c` into `verify.{h,c}` so OTA and
  normal boot share the same SHA + ECDSA correctness path), and
  `flash_slot_commit_metadata` for the atomic active-slot swap.
  Refuses to overwrite the currently-active slot.
- **`tools/ota_send.py`** + **`tools/_framing.py`** — host driver.
  `_framing.py` is the Python mirror of `lib/framing/` (CRC,
  stuffing, decoder).  `ota_send.py` is the user-facing tool:
  PING → OTA_BEGIN → loop OTA_CHUNK → OTA_END → STATUS, with
  per-chunk progress and sliding-window-1 retry.
- **HIL `scripts/run_ota_test.py`** — wired into CI.  Two passes:
  clean OTA flips active A → B and the new app boots; tampered
  OTA reports STATUS=verify_failed and slot A stays active.

The first 16 KB sector is now at **15604 / 16384 bytes** —
about 780 bytes of headroom, comfortably under the cap.  The
sector-0 budget guard in `apps/bootloader/loader/Makefile`
keeps it enforced.

Active-slot swap is power-cut safe by design:

- A power cut **before** the new metadata commits leaves both
  slots untouched.
- A power cut **between** writing the target's `active=1` and
  clearing the previous slot's `active=1` leaves both bits set;
  the existing slot-pick decision tree falls back to the higher
  `monotonic_counter`, which is by construction the freshly-OTA'd
  slot.

`docs/wiki/plans/001-bootloader/ota.md` documents the wire
protocol, the receiver state machine, the swap window, the
operator-forced recovery procedure (write the magic via OpenOCD
if both slots are bricked at the app layer), and the production
gap (no transport authentication / replay protection beyond the
per-image signature, by design — Plan 001 §"Out of scope").

## [2026-05-31] milestone | Plan 001 Phase 1.8 (part 1) — `lib/framing/` (#162)

First half of Phase 1.8 lands: the reusable framing middleware. HDLC-style
byte stream with `FLAG=0x7E`, `ESC=0x7D` byte-stuffing, CRC-16-CCITT
(poly `0x1021`, init `0xFFFF`, no final XOR) over the unstuffed
`[SEQ][TYPE][LEN_lo][LEN_hi][PAYLOAD]` span. Max payload 1024 B, frame
types cover the OTA path (`OTA_BEGIN`/`CHUNK`/`END`), the link path
(`DATA`/`ACK`/`NACK`), and ping/status.

Three layers in one lib:

- **Encoder** (`frame_encode`) — pure; computes worst-case stuffed size,
  rejects oversize / NULL / bad-type / too-small-out-buf inputs.
- **Decoder** (`frame_decoder_t`, `frame_decoder_feed`) — stateful byte
  pump, fires an `rx_cb` on each CRC-valid frame and an `err_cb` on CRC
  mismatch / truncation / oversize. Resyncs on the next `FLAG`.
- **Reliable layer** (`frame_link_t`) — sliding-window-of-1 stop-and-wait
  ARQ with timeout-based retransmit, NACK retransmit, configurable
  retries, and duplicate-SEQ suppression on the receive side. Transport-
  agnostic: caller hooks `write` and `now_ms`, so the same code drives
  UART today and SPI in Plan 002 later.

35 host unit tests (`tests/lib/framing/`) cover round-trip, byte-stuffing
of `FLAG`/`ESC` payloads, byte-stuffing of `SEQ`, CRC mismatch dropping,
truncation + resync, garbage-prefix silently dropped, idle-FLAG pair,
oversize payload dropped, double-ESC malformed stuffing, trailing ESC
before FLAG, byte-at-a-time feed, full-1024-byte payload round-trip,
decoder reset, and the full link layer (init / send / overlap rejection /
oversize rejection / write failure / ACK / NACK retry / timeout
retransmit / max-retries exhausted / RX dedup). Coverage on
`lib/framing/src/framing.c` is 95.3% (the 4.7% miss is the redundant-but-
harmless transport-write-failure branch inside `link_retransmit` — the
tick path exercises it via the timeout side instead). Meets the ≥95%
bar Plan 002 §2.2 sets for this layer.

The OTA receiver, app-side `ota_request` CLI command, `tools/ota_send.py`
host driver, and HIL `run_ota_test.py` land in the second half of Phase
1.8 in a follow-up PR — the framing layer is independently useful and is
small enough to review on its own.

## [2026-05-31] milestone | Plan 001 Phase 1.7 — A/B slots and fallback (#158)

The bootloader now reads two slot-metadata blobs from sectors 1 and 2,
picks an active slot using a deterministic decision tree, verifies it,
and falls back to the other slot on any failure. If both fail, it logs
`BL: both slots failed verify` and halts. Phase 1.6's verify call is
reused unchanged inside a small wrapper; this phase only adds the
slot-pick + retry around it.

New `SLOT={A,B}` Makefile knob: any app linked with `app_ls.ld` can
target either slot. Slot-B output paths carry a `_b` suffix so they
do not clobber slot-A artefacts. `make EXAMPLE=cli_simple SLOT=B`
produces a slot-B-linked signed image at 0x08040000.

`lib/flash/` middleware lands with `flash_slot_validate_range()`
(refuses sector-0 overlap), `flash_slot_erase()`, and
`flash_slot_commit_metadata()` (erase-then-program-then-readback,
power-cut-safe). 14 host unit tests cover the validators; the
mutating helpers are exercised by the new HIL test.

`tools/partition_dump.py` decodes both metadata blobs and image
headers from a connected board (or a 512 KB flash dump). Useful for
HIL debugging and for confirming the right slot is active after an
OTA-style swap.

`scripts/run_ab_slot_test.py` wires four HIL passes into CI:

- clean A active → boots A, no fallback
- clean B active → boots B, no fallback
- A corrupt + active, B clean → fallback to B, app boots
- both corrupt → halt, app never runs

All four pass on the dev board with verify times ~143 ms (slot A) and
~149 ms (slot B). loader.bin grew from 10 748 → 11 692 bytes
(~4.5 KB headroom in sector 0).

Scope cut: rollback-on-crash semantics (bootloader incrementing
`fail_count` before each jump) deferred to land alongside Phase 1.9
anti-rollback writes. The metadata struct already carries the field;
the bootloader does not yet write it.

## [2026-05-31] milestone | Plan 001 Phase 1.6 — verify-and-jump (#156)

Bootloader now verifies the slot-A image before jumping: SHA-256 over the
payload is constant-time-compared against `hdr.sha256`, then
`crypto_ecdsa_p256_verify()` checks `hdr.signature` against the linked-in
`bootloader_pubkey[64]`. The DWT cycle counter brackets both calls and the
result is logged as `BL: verify ok in <cycles> cycles (~<ms> ms)`. Failures
emit a distinct `BL: verify FAILED: <reason>` line and halt — no slot-B
fallback yet (Phase 1.7).

`libcrypto.a` (SHA-256 ~1 KB + micro-ecc P-256 verify ~5 KB) is linked into
the loader; final `loader.bin` measures 10 748 / 16 384 bytes, leaving
~5.6 KB headroom. A post-link `stat` guard in the bootloader Makefile
fails the build if sector 0 is ever exceeded — the linker's `_etext` ASSERT
does not see the `.data` LMA copy, so the bin-level check is the
authoritative final guard.

A new `scripts/run_verify_test.py` HIL job runs after the boot smoke test:
flashes the clean `app_blinky_signed.signed.bin` and asserts both `BL:
verify ok` and `APP: blinky alive` appear; then flips a byte in the
payload region (file offset `payload_offset + 4`, NOT inside the header —
that would only trip CRC and never reach the verify path) and asserts
`BL: verify FAILED:` with no `APP: blinky alive`. `tests/baselines/
bootloader_verify.json` records the soft 50 % tolerance plus the 500 ms
hard cap from Plan 001 §1.3.

See [plans/001-bootloader/verify-and-jump.md](plans/001-bootloader/verify-and-jump.md).

## [2026-05-30] milestone | Plan 001 Phase 1.5 — bootloader skeleton (#151)

The first on-target piece of the bootloader track lands in this PR. Sector 0
(`0x08000000`, 16 KB) now holds `apps/bootloader/loader` linked with
`linker/bootloader_ls.ld`; every other app — including `cli_simple` for HIL
— is linked at slot A (`0x08010000`) via `linker/app_ls.ld` and post-
processed by `tools/sign_image.py` into a `.signed.bin` carrying the
140-byte `img_header_t`. The shared startup code writes
`SCB->VTOR = &_app_vector_base` for every slot-A image; the bootloader
build leaves the symbol weakly-undefined and skips the write.

Signature verification is intentionally skipped at this stage — the
bootloader trusts whatever lives in slot A, parses the header (magic + CRC
+ type), and jumps. Phase 1.6 will wire `crypto_ecdsa_p256_verify()`
against the public key already linked into the loader binary.

Notable shape decisions:

- **Dev keypair from a fixed seed at build time.** `make keys` runs
  `tools/keygen.py --seed stm32-bare-metal-dev-fixture` into
  `build/keys/`, which is gitignored. CI regenerates on every run; no
  private key ever lands in source control. Production keys obviously
  would not work this way (Phase 1.11 production-gap doc).
- **Migrate every app to slot A.** The default `LDSCRIPT` in
  `Makefile.common` flips from `linker/stm32_ls.ld` to
  `linker/app_ls.ld`, and every `apps/basic/*.c` and `apps/cli/cli_simple`
  now produces a `.signed.bin` as its final artifact. Trying to flash an
  unsigned binary at sector 0 is rejected by the new
  `make flash-bootloader` / `make flash` split.
- **HIL runner unchanged from the test side.** `scripts/run_hil_tests.py`
  now flashes `cli_simple.signed.bin` at `0x08010000` instead of the
  `.elf` at sector 0; the existing `START_TESTS`/`END_TESTS` framing is
  untouched. A new `scripts/run_boot_smoke_test.py` flashes
  `app_blinky_signed.signed.bin` and asserts the bootloader → app jump.
- **Bootloader is manually flashed.** A new `make flash-bootloader`
  target programs sector 0; CI never invokes it. Each NUCLEO board owned
  by the rig needs the bootloader installed once before that board can
  host HIL runs. Procedure + recovery documented in
  `docs/wiki/plans/001-bootloader/bootloader-skeleton.md`.

## [2026-05-29] merge | Plan 001 Phase 1.4 — host signing tooling (#148)

`tools/keygen.py` and `tools/sign_image.py` produce signed firmware images
consumable by the future bootloader. Both share `tools/_img_format.py` — a
single Python source of truth for the on-flash format (struct layout, magic
constants, CRC-32 algorithm) that mirrors `lib/img/inc/img_header.h`. The
PEM private key is gitignored; the public key is emitted as a C source so
it can be linked into the bootloader.

Cross-language compatibility is enforced by a new round-trip suite at
`tests/tools/sign_roundtrip/`: `make all` runs `keygen.py` + `sign_image.py`
to produce fixtures, then a Unity test parses the result with the real C
parser (`lib/img`) and verifies the signature with `lib/crypto`. Four
cases — happy path, tampered payload (digest mismatch), tampered signature
(verify returns 0), tampered header (CRC mismatch). Any drift between the
Python tools and the C parser fails host-tests on the next CI run.

Workflow documented in `docs/wiki/plans/001-bootloader/signing.md`.

## [2026-05-20] milestone | Repository refactor — Plan 000 landed (#136)

Single PR with five commits, one per phase, to prepare the repo for multi-track
work (bootloader/security and comms+DSP):

1. `examples/` → `apps/`. The CLI app is the primary build target, not an
   example. `git mv` preserves history. Build paths under `build/apps/...`,
   internal var `EXAMPLES_DIR` → `APPS_DIR`. User-facing `EXAMPLE=` make var
   kept for backward compatibility with CI and documented invocations.
2. New `lib/` directory for middleware (no `main()`, no hardware ownership).
   Each lib mirrors `drivers/Makefile` and builds to
   `build/lib/<name>/lib<name>.a`. A `lib/skeleton/` stub library plus a
   one-test Unity suite under `tests/lib/skeleton/` proves the plumbing
   end-to-end. Real libs (crypto, framing, modem) land with Plans 001/002.
3. Per-app linker script override: `LDSCRIPT` switched from `:=` to `?=`
   and `LDFLAGS` from `:=` to `=` (recursive expansion) so an app's Makefile
   can select its own linker script after `include ../../Makefile.common`.
   Default `linker/stm32_ls.ld` unchanged for every existing app.
4. New empty `tools/` directory with a README documenting the convention for
   host-side firmware utilities (image signers, OTA flashers, BER plotters).
   Distinguished from `scripts/` which is repo automation.
5. Documentation refresh — `architecture.md`, `index.md`, `roadmap.md`,
   `testing.md`, `iwdg.md`, `ci.md`, `README.md`, `CLAUDE.md`, and Plan 000
   itself all updated. Historical log entries that reference `examples/...c`
   are intentionally not rewritten; they describe the repo as it was at the
   time.

## [2026-04-22] milestone | Implement IWDG watchdog driver (#68)

Added Independent Watchdog (IWDG) driver: `iwdg_init(timeout_ms)`, `iwdg_feed()`,
`iwdg_was_reset_cause()`, `iwdg_clear_reset_flags()`. Pure calculation functions in
`iwdg_calc.h`: prescaler/reload solver, timeout computation, prescaler divider lookup.
37 host unit tests covering calc functions, register writes, and boundary cases.
Basic example `iwdg_basic.c` demonstrates 1-second watchdog with LED feedback.
Wiki page: `docs/wiki/drivers/iwdg.md`.

## [2026-04-17] infra | Add JUnit XML reporting for HIL tests in CI (#123)

Extended `scripts/run_hil_tests.py` with a `--junit-xml PATH` argument (default:
`hil-test-results.xml`) and a `write_junit_xml(results, regressions, output_path)`
function. Unity test lines map to `<testcase classname="test_harness">` elements;
`TEST:` sampled performance lines map to `<testcase classname="spi_perf">` elements.
Baseline regressions and integrity failures become `<failure>` child elements with
descriptive messages. The XML is written in a `finally` block so it is always produced,
even on serial timeout or build error, ensuring the report is always available in CI.
Updated `check_baselines` to return regression details alongside the pass/fail bool.
Updated `.github/workflows/ci.yml`: the `hil-tests` job now passes `--junit-xml
hil-test-results.xml` and adds a `Publish HIL test results` step using
`dorny/test-reporter@v3` (`if: always()`, `fail-on-error: false`) — every PR now
shows a **HIL Tests** tab in the GitHub Test Summary UI, mirroring the existing
**Unity Tests** tab from the `host-tests` job.

---

## [2026-04-17] milestone | HIL Tier 4: SysTick hardware tests (#62)

Added three HIL tests to the Tier 4 section of `examples/cli/test_harness.c`:
`test_systick_get_ms_increments` — calls `systick_get_ms()` twice with a
`timer_delay_us(5000)` between them and asserts the difference is 5 ± 1 ms;
`test_systick_elapsed_since` — records a start snapshot, delays 10 ms, and asserts
`systick_elapsed_since(start)` is 10 ± 1 ms; `test_systick_delay_ms_accuracy` —
measures `systick_delay_ms(10)` via DWT cycle counter (expected 1 000 000 cycles at
100 MHz) and asserts within ±100 000 cycles (±1 ms, reflecting inherent 1 ms
quantisation of the tick counter). All three tests pass on hardware (actual
`delay_ms` measurement: ~959 000 cycles). Added `systick_init()` call to
`examples/cli/cli_simple.c` `main()` so the SysTick ISR is running before the
test harness executes. Total HIL tests: 80.

## [2026-04-17] milestone | SysTick tick counter with non-blocking API (#62)

Refactored the SysTick driver from a polled-COUNTFLAG blocking loop to an
ISR-driven millisecond counter. `systick_init()` configures SysTick for 1 ms
interrupts using the processor clock and sets priority to `IRQ_PRIO_TIMER` via
`NVIC_SetPriority(SysTick_IRQn, ...)`. `SysTick_Handler` increments a static
`volatile uint32_t s_tick_ms` counter. `systick_get_ms()` returns the counter
value; `systick_elapsed_since(start)` uses unsigned subtraction for
wraparound-safe elapsed time measurement. `systick_delay_ms()` now polls the
tick counter instead of COUNTFLAG, and returns immediately for delay == 0.
Added `uptime` CLI command that prints boot time as `hh:mm:ss.mmm`.
Added `systick_reset_for_test()` (guarded by `#ifdef UNIT_TEST`) and 14 new
host unit tests in `tests/systick/`. Extended `tests/driver_stubs/core_cm4.h`
with `SysTick_CTRL_*` constants and negative-IRQn handling in
`NVIC_SetPriority`.
## [2026-04-17] milestone | Multi-instance UART driver with configurable baud rate (#69)

Refactored `drivers/src/uart.c` and `drivers/inc/uart.h` to support USART1, USART2,
and USART6. A static hardware descriptor table (`uart_hw_table`) maps each
`uart_instance_t` to its registers, RCC enable bit, GPIO pins (TX/RX), DMA stream IDs
and channels, IRQn, and APB clock getter. New `uart_init_config(const uart_config_t *cfg)`
accepts an instance + baud rate; `uart_init()` is kept unchanged as a USART2/115200
wrapper for backward compatibility with all existing callers (examples, log_platform,
tests). Baud divisor is computed via the correct APB clock for each instance
(`rcc_get_apb1_clk()` for USART2, `rcc_get_apb2_clk()` for USART1/USART6).
Added `fake_USART1` and `fake_USART6` to the driver test stubs. Added 25 new host tests
covering USART1 GPIO pinout, APB2 BRR, NVIC entries, USART6 GPIO pinout, APB2 BRR,
NVIC entries, and `uart_init_config` invalid-argument guards. Total UART tests: 76
(up from 46); total host tests: 328.

## [2026-04-20] milestone | HIL SPI throughput: warm-up run + 5-sample median (#112)

Made HIL SPI performance tests robust against transient loopback corruption and measurement
variance. Two changes: (1) each test now runs 5 back-to-back transfers and reports the
median cycle count, with a majority-vote integrity check (≥4/5 byte-match passes required);
(2) one untimed warm-up transfer runs before the 5 measured samples to pay the one-time
`spi_dma_init_streams()` cost (DMA clock enable, stream CR/PAR config, NVIC setup) outside
the measurement window — all 5 samples then reflect steady-state per-transfer cost, matching
production usage where DMA is initialised once at startup. Extended `TEST:` output format
adds `:samples=N:integrity_passes=M` fields. All 57 baselines recalibrated from warm hardware
runs; small-buffer DMA entries (1B/4B) dropped ~2% vs prior median, confirming the cold first
sample was inflating previous values. Total HIL tests: 73 (SPI/FPU/RCC/Timer/UART unchanged).

## [2026-04-20] milestone | HIL Tier 5: GPIO and EXTI loopback tests (#99)

Added GPIO output/input and EXTI interrupt tests to the HIL harness, reusing the UART
loopback cables already wired on the board (PA9↔PB7 for UART1, PC6↔PC7 for UART6).
GPIO tests: configure one pin as push-pull output and the other as floating input; assert
HIGH, LOW, and toggle propagate through the cable. EXTI tests: arm EXTI line 7 (port B,
PB7) with a minimal `EXTI9_5_IRQHandler` that increments a volatile counter; drive PA9 to
trigger rising and falling edges; assert counter increments; also tests `exti_software_trigger`.
Implemented as 4 consolidated test functions (2 GPIO + 2 EXTI) to minimise serial output and
pin reconfiguration overhead — each function covers all conditions for one loopback pair in a
single init/settle/deinit cycle. Total HIL tests: 77.

## [2026-04-15] milestone | Driver host tests: UART (#100)

Added `tests/uart/` with 46 tests covering the UART driver in `drivers/src/uart.c`.
Tier 1 (register config): `uart_init` CR1/CR2/BRR setup, DMA TX/RX enable, NVIC configuration,
GPIO alternate function pinout for USART2. Tier 2 (pure functions via `uart_calc.h`):
`uart_compute_baud_divisor` rounding at multiple clock/baud combinations; `uart_circular_bytes_available`
wrap-around arithmetic for all cases (no wrap, single wrap, full buffer, empty buffer).
ISR path tests: RXNE callback dispatch, DMA-RX active suppression, error flag handling
(ORE/FE/NF), `uart_clear_errors` reset. Total host tests: 298.

## [2026-04-14] milestone | Driver host tests: RCC and Timer (#101)

Added `tests/rcc/` (36 tests) and `tests/timer/` (52 tests).
RCC Tier 1: `rcc_init` register sequence (HSI→PLL→SYSCLK, AHB/APB prescalers, Flash latency,
PWR voltage scaling), clock getter functions (`rcc_get_sysclk`, `rcc_get_apb1_clk` etc.).
RCC Tier 2 via `rcc_calc.h`: `rcc_compute_pll_config` PLL factor solver across multiple
source/target combinations; `rcc_compute_apb_prescaler`; `rcc_compute_flash_latency` wait-state
lookup. Timer Tier 1: TIM2–TIM5 clock enable, ARR/PSC/CCR register setup for basic, PWM, and
one-pulse modes; NVIC enable/disable paths. Timer Tier 2 via `timer_calc.h`:
`timer_compute_pwm_psc` across frequency/step combinations; `timer_compute_duty_ccr` boundary
cases (0%, 50%, 100%). Total host tests: 252.

---

## [2026-04-17] milestone | Add driver host tests for GPIO and EXTI (#99)

Added `tests/gpio/` (44 tests) and `tests/exti/` (56 tests) using the fake peripheral stub
infrastructure. GPIO tests cover all public API functions: clock enable/disable (RCC AHB1ENR
bits), `gpio_configure_pin` (MODER 2-bit fields), set/clear/toggle (BSRR/ODR), read (IDR),
`gpio_set_af` (AFR nibbles), output type (OTYPER), speed (OSPEEDR), pull (PUPDR), and
`gpio_configure_full` combined call. Also validates port routing (each port maps to its own
fake struct). EXTI tests cover: `exti_configure_gpio_interrupt` (SYSCFG EXTICR port mapping
for all 6 GPIO ports across all 4 EXTICR registers, RTSR/FTSR trigger types, IMR/EMR mode,
NVIC enable via `NVIC_EnableIRQ`), `exti_enable_line`/`exti_disable_line` (NVIC ISER/ICER),
`exti_set_interrupt_mask`/`exti_set_event_mask` (EXTI IMR/EMR), `exti_is_pending`/
`exti_clear_pending` (EXTI PR), and `exti_software_trigger` (EXTI SWIER). Updated
`tests/Makefile` to include the `exti` suite. Total host tests: 164 (CLI + string_utils + GPIO + EXTI).

---

## [2026-04-17] infra | Add parallel agent worktree workflow (#114)

Added `scripts/worktree_new.sh` and `scripts/worktree_clean.sh` for creating and cleaning
isolated git worktrees for parallel agent work. Each worktree gets its own branch based on
`origin/main` and lives at `../stm32-bare-metal-worktrees/<branch>/`. Updated `CLAUDE.md`
with a `## Parallel Agent Workflow (Worktrees)` section covering the full 8-step workflow,
HIL serialisation constraint, and cleanup procedure. Added `docs/wiki/agents.md` with
detailed parallelism rules and troubleshooting.
Compatible with `CLAUDE_CODE_EXPERIMENTAL_AGENT_TEAMS=1` and the `Agent` tool's
`isolation: "worktree"` parameter (harness auto-creates worktrees in that case).

---

## [2026-04-13] infra | Add Tailscale remote access and MCP HIL server (#109)

Added `scripts/mcp_hil_server.py` — a Python stdio MCP server that exposes `hil_status` and
`hil_run_tests` tools to Claude Code. Claude can now autonomously build, flash, and run HIL
tests on the real NUCLEO board during development: it rsyncs the working tree to the Pi (so
uncommitted changes are included), runs `run_hil_tests.py` remotely, and receives structured
JSON results (pass/fail, metrics, regressions). Reuses `parse_test_output` and `load_baselines`
from the existing script without duplication. Registered via `.mcp.json` at project root.
Remote access enabled by Tailscale mesh VPN; configuration via `HIL_PI_SSH` env var.
See `docs/wiki/hil-remote-access.md`.

## [2026-04-13] infra | Add hil-tests CI job on self-hosted Pi runner (#86, #105)

Added `hil-tests` job to `.github/workflows/ci.yml` running on `[self-hosted, pi-hil]`
with `needs: host-tests`. The Pi runner is registered and idle. Also fixed GCC 14 linker
compatibility (`.ARM.exidx` section), throughput calculation truncation (switched to float),
and updated all performance baselines. PR #106.

## [2026-04-12] milestone | HIL test infrastructure with Unity on target (#86)

Implemented hardware-in-the-loop testing framework. Unity compiled for ARM target with `UNITY_OUTPUT_CHAR=_putchar` to route output through UART (avoids libc putchar Hard Fault on bare-metal). Build controlled by `HIL_TEST=1` flag — production builds unchanged (~19 KB), HIL builds ~24 KB. Test harness uses parameterized `RUN_SPI_TEST` macro for 60 tests: all 5 SPI interfaces at max speed (Tier 1), deep prescaler/buffer-size sweep on SPI2 (APB1) and SPI1 (APB2) (Tier 2), plus FPU tests (Tier 3). Machine-parseable output format (`TEST:name:PASS:cycles=N:metric=N`) with `START_TESTS`/`END_TESTS` markers. Python automation script (`scripts/run_hil_tests.py`) handles build → flash → serial capture → parse → baseline validation. Performance baselines stored in `tests/baselines/performance.json` with per-test tolerance thresholds. Key finding: DMA crossover at ~16 bytes (below that, polled is faster due to DMA setup overhead). All infrastructure ready for CI integration — only Pi runner registration remains (#86).

## [2026-04-12] merge | Build driver host test infrastructure + GPIO tests (#98)

Created `tests/driver_stubs/` — a test-only header layer that intercepts `#include "stm32f4xx.h"` via include path ordering, includes the real `stm32f411xe.h` for accurate TypeDefs and bit-flag constants, then overrides all peripheral instance macros to point at global fake structs in SRAM. Companion `core_cm4.h` stub provides fake NVIC (inspectable struct), SysTick, SCB, DWT and stubs for Cortex-M intrinsics. `test_periph_reset()` zeroes all fakes in setUp(). GPIO driver test suite: 44 tests covering clock enable/disable, MODER, BSRR, ODR, IDR, AFR, OTYPER, OSPEEDR, PUPDR and port routing. Driver code is unchanged. Total host tests: 108.

## [2026-04-12] infra | Refresh roadmap + define driver host testing strategy (#97)

Rewrote `roadmap.md` with all 15 open GitHub issues categorised and prioritised. Added "Testing Architecture" section to `architecture.md` documenting the three-layer test pyramid and the fake peripheral stub mechanism. Added "Driver Testing Strategy" section to `testing.md` covering both tiers (fake stubs + pure function extraction) with the include-path override mechanism and pre-seeding pattern. Opened 4 new issues: #98 (infra), #99 (GPIO/EXTI), #100 (UART), #101 (RCC/Timer).

## [2026-04-12] merge | Add host test coverage reporting (#88)

Test Makefiles now accept `EXTRA_CFLAGS`. `tests/Makefile` gains a `coverage` target: clean rebuild with `--coverage`, `lcov --capture`, `lcov --extract '*/utils/src/*'`, `genhtml`. CI installs `lcov` and runs `make -C tests coverage` after tests pass, uploading `tests/coverage-html/` as the `coverage-report` artifact via `actions/upload-artifact@v6` (Node.js 24). Coverage artifacts added to `.gitignore`.

## [2026-04-12] merge | Add firmware-build CI job; update CLAUDE.md pre-push rules (#87)

Added `firmware-build` job to `ci.yml` — installs `gcc-arm-none-eabi` via apt, runs `make all` in parallel with `host-tests`. Catches cross-compilation errors on every PR. Updated `CLAUDE.md` to require both `make test` and `make all` to pass before pushing. `firmware-build` still needs to be added as a required check in branch protection after its first run on `main`.

## [2026-04-12] merge | Make Unity a direct root-level submodule (#84)

Added `3rd_party/unity/` as a direct submodule (ViniBR01/Unity fork, commit 36e9b19), replacing the fragile nested path `3rd_party/log_c/3rd-party/unity/`. Updated both test Makefiles. The Unity copy inside log_c is untouched.

## [2026-04-12] merge | Add JUnit XML test reporting to CI (#85)

Added `tests/unity_to_junit.py` to convert Unity stdout (`file:line:name:PASS/FAIL`) to JUnit XML. Updated `ci.yml` to capture `make test` output with `tee` (preserving exit code via `set -o pipefail`), convert to XML, and publish via `dorny/test-reporter@v3` (Node.js 24). Every PR now shows a Test Summary tab with per-test pass/fail detail.

## [2026-04-12] merge | Upgrade actions/checkout to v6 (Node.js 24) (#89)

Replaced `actions/checkout@v4` (Node.js 20, deprecated) with `actions/checkout@v6.0.2` (Node.js 24). Eliminates the deprecation warning that appeared in every CI run. Resolves before the forced cutover deadline of 2026-06-02.

## [2026-04-12] infra | Add CLAUDE.md and project wiki (#90)

Set up Claude Code project customization. Added `CLAUDE.md` with development workflow rules, build commands, and wiki schema. Created `docs/wiki/` with initial pages covering architecture, roadmap, testing, CI, all drivers, and ADR 001.

## [2026-04-11] merge | Add GitHub Actions CI pipeline (#83)

Created `.github/workflows/ci.yml` with `host-tests` job on `ubuntu-latest`. Runs `make test` on every PR targeting `main` and on every push to `main`. Cancels in-progress runs on new commits. Branch protection on `main` requires `host-tests` to pass. See ADR 001.

## [2026-04-11] merge | Add host unit tests for CLI engine and string utils (#81)

Added Unity-based host unit tests. 41 tests for `utils/src/cli.c` (CLI engine), 23 tests for `utils/src/string_utils.c`. Tests compile with native `gcc` using header stubs for embedded-only includes. Total: 64 tests.

## [2026-02-23] merge | Implement reusable general-purpose timer driver (#80)

Added `drivers/timer.c` covering TIM2–TIM5. Supports basic timer with update interrupt callback, PWM output (mode 1, preload), and `timer_delay_us` using TIM5 in one-pulse mode.

## [2026-02-21] merge | Extract DMA into a reusable generic driver (#79)

Extracted DMA logic from UART/SPI into `drivers/dma.c`. Stream allocation model, transfer-complete and error callbacks, `dma_stream_start_config` fast-path for circular RX.

## [2026-02-21] merge | Harden linker script with stack/heap sections and overflow detection (#78)

Added explicit stack and heap sections to `linker/stm32_ls.ld` with overflow detection symbols. Startup code checks stack canary at boot.

## [2026-02-20] merge | Implement fault handlers with register dump (#77)

Added HardFault, BusFault, UsageFault, MemManage handlers in `drivers/src/fault_handler.c`. On fault: dumps R0–R15, PC, LR, xPSR, and fault status registers over UART.

## [2026-02-20] merge | Enable FPU in startup code (#76)

Enabled the Cortex-M4F hardware FPU (CP10/CP11 coprocessors) in startup code. Added `FPU_ENABLE` build flag (default: 1). Compiler flags: `-mfloat-abi=hard -mfpu=fpv4-sp-d16`.

## [2026-02-18] merge | Enhance GPIO driver with OTYPER, OSPEEDR, PUPDR configuration (#75)

Added `gpio_set_output_type`, `gpio_set_speed`, `gpio_set_pull`, and `gpio_configure_full` to the GPIO driver. Previously only mode and AF were configurable.

## [2026-02-17] merge | Add RCC clock configuration driver with PLL support to reach 100 MHz (#74)

Added `drivers/src/rcc.c`. Configures HSI → PLL → SYSCLK at 100 MHz. APB1 at 50 MHz, APB2 at 100 MHz.

## [2026-02-17] merge | Use DMA to improve SPI throughput in loopback test (#60)

Added `spi_transfer_dma` and `spi_transfer_dma_blocking` to the SPI driver. DMA TX/RX configured per SPI instance.

## [2025-11-05] merge | Add shift register example via SPI (#50)

Added `examples/basic/shift_register_simple` — controls an SN74HC595 shift register using SPI.

## [2025-11-04] merge | Refactor logging module integration (#46–#48)

Switched log_c to a self-contained implementation (no printf dependency, ~1.8 KB). Added `drivers/src/log_platform.c` as the platform integration layer with callback-based backend registration and runtime log level control via `log_platform_set_level()`.

## [2025-10-31] merge | Add PWM example (#44)

Added `examples/basic/blink_pwm` — LED breathing/fade using TIM2 PWM output.

## [2025-10-30] merge | Add timer interrupt example (#43)

Added `examples/basic/timer_interrupt` — TIM2 update interrupt at 1 Hz.
