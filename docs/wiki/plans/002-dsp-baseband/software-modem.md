# Plan 002 Sub-track B0 — Self-Contained Software BPSK Modem (q15)

**Status:** proposed
**Parent plan:** [Plan 002 — Inter-Board Comms + DSP Baseband](../002-comms-and-dsp-baseband.md)
**Tracking issue:** #138
**Depends on:** Plan 000 (repo refactor — `lib/`, `apps/dsp/`, `tools/`). No hardware fixture, no second board.

## Why this exists / what changed from the original plan

The original Plan 002 sub-track B leads with a **physical analog-link fixture** (PWM-DAC → RC
low-pass filter → ADC) before any modem code runs. That front-loads hardware ceremony — a
breadboard, RC filter, a second NUCLEO, careful ADC/PWM wiring — onto work whose interesting
substance is the **baseband signal processing**, not the wiring.

This sub-track reorders the work around one idea the user named directly: **wireless communication
emulation**. If the channel is a *software function* — `samples_out = channel(samples_in, snr)` —
then the entire transmit/receive chain becomes **self-contained on a single board**. This is
exactly how production modems are developed: the whole transceiver is validated in simulation
against theory *before* an RF (or in our case, analog-loopback) front-end is ever attached.

The architectural seam:

```
   bits ──▶ [TX chain] ──▶ samples ──▶ [CHANNEL] ──▶ samples ──▶ [RX chain] ──▶ bits ──▶ BER
                                          ▲
                          swappable backend behind ONE interface:
        B0 (this doc): software AWGN + impairments   ← self-contained, no hardware
        Later:         PWM-DAC → RC → ADC loopback    ← single board, one wire
        Later:         board A TX → wire → board B RX  ← two boards (original 2.1/2.8)
```

The TX/RX DSP core **never changes** as backends swap. So this self-contained software modem *is*
the foundation; the analog-link and two-board fixtures from the original plan become later,
optional channel backends rather than prerequisites.

## Design decisions (locked)

| Decision | Choice | Rationale |
|---|---|---|
| First useful milestone | **Symbol-level BER vs SNR** (PRBS + BPSK + AWGN), validated against theory | Smallest self-contained "it works"; no waveforms/pulse-shaping yet. |
| Numeric representation | **Fixed-point q15** (`int16_t`, Q1.15) for the sample path | Authentic to how cost-sensitive real modems run; exercises overflow/scaling discipline. The hard-FPU stays available for scalar control-loop math (AGC, Eb/N0→variance). |
| DSP library | **Hand-rolled kernels** initially | Matches the repo's "no HAL, implement it yourself" ethos and keeps flash small. CMSIS-DSP can be vendored later if/when FFT or large filters are needed (deferred to expansion). |
| Where it runs | On-target firmware app `apps/dsp/modem_sim`, CLI-driven; same code host-tested | Fits the existing three-layer test pyramid and DWT cycle-baseline culture. |

### Fixed-point conventions (apply to every sample-path module)

- **Sample type:** `q15_t` = `int16_t`, format **Q1.15** — range [−1.0, +1.0), where `0x7FFF ≈ +0.99997`
  represents `+1.0` and `0x8000` represents `−1.0`.
- **Products** use `q31_t` (`int32_t`) accumulators: `q15 * q15 → q30`, shift `>> 15` to return to q15.
- **Saturation:** all q15 outputs saturate (no wraparound) via a shared `q15_sat(int32_t)` helper.
- **Rounding:** add `(1 << 14)` before the `>> 15` shift where bias matters (matched filter taps).
- **Golden references:** Python (`numpy`) generates reference vectors in float, quantised to q15
  with a documented tolerance (typically ±2 LSB) so host tests compare target output to a fixed,
  checked-in expected array — mirroring `tests/lib/crypto/vectors/`.

These conventions live in a single header `lib/dsp/inc/fixed.h` so every later module shares them.

## Module map

All new code is pure C with **no peripheral access** in the `lib/` modules (so the exact same
sources compile for host unit tests and on-target), mirroring `lib/framing/` and `lib/img/`.

| Module | Path | Responsibility |
|---|---|---|
| Fixed-point helpers | `lib/dsp/inc/fixed.h` | q15 type, saturate, mul, round-shift, dB↔linear. Header-only where possible. |
| PRBS generator/checker | `lib/prbs/` | PRBS-9 / PRBS-15 LFSR bit source + self-synchronising error counter. |
| BPSK modem core | `lib/modem/` | bit→symbol map (0→−1, 1→+1 in q15), symbol→bit slice/demap, BER accounting. |
| AWGN channel | `lib/channel/` | Seedable Gaussian noise (Box-Muller, deterministic PRNG), Eb/N0→noise-variance, add-to-samples. |
| RRC pulse shaping | `lib/dsp/` (later phase) | upsample + root-raised-cosine FIR (q15 taps), matched filter, symbol decimation. |
| FEC | `lib/fec/` (later phase) | Hamming(7,4) encode / decode-and-correct, pure functions. |
| App | `apps/dsp/modem_sim/` | CLI front-end: `modem run`, `modem sweep`; DWT cycle reporting. |

Host tests land under `tests/lib/prbs/`, `tests/lib/modem/`, `tests/lib/channel/`, `tests/lib/dsp/`,
`tests/lib/fec/` — one subdir per module, each with its own `Makefile` and `test_*.c`, exactly like
the existing `tests/lib/framing/` and `tests/lib/crypto/` suites.

## Phases — each is one issue / one PR

### Phase B0.1 — `lib/prbs` + `lib/modem` BPSK core (symbol level, no noise)

**Scope**
- `lib/dsp/inc/fixed.h`: q15 type + `q15_sat`, `q15_mul`, `q15_from_float` (host/test only), dB helpers.
- `lib/prbs/`: PRBS-9 (x⁹+x⁵+1) and PRBS-15 (x¹⁵+x¹⁴+1) generators; a checker that locks to the
  sequence and counts bit errors over a stream (the standard self-synchronising BER technique).
- `lib/modem/`: `bpsk_map(bit) → q15 symbol` (0 → −1.0, 1 → +1.0), `bpsk_slice(q15) → bit`,
  and a `ber_counter` that compares demapped bits to the PRBS reference.
- No channel yet — TX symbols feed straight into RX. BER must be exactly 0.

**Host tests**
- Exhaustive map/slice round-trip (both bits, boundary q15 values, exact-zero tie-break documented).
- PRBS period correctness (PRBS-9 period = 511, PRBS-15 = 32767), checker locks and reports 0 errors
  on a clean stream and the exact injected count on a corrupted stream.

**Validation:** `make test` green; zero-error clean round-trip; coverage ≥95% on both modules.

### Phase B0.2 — `lib/channel/awgn` + end-to-end symbol BER vs SNR ⭐ first useful milestone

**Scope**
- Deterministic, seedable PRNG (e.g. xorshift128) so runs are reproducible and CI-stable.
- Box-Muller transform → unit-variance Gaussian; scale to the noise σ implied by a target **Eb/N0**.
- Eb/N0 (dB) → σ mapping for BPSK documented in the header with the derivation.
- `channel_awgn_apply(q15 *samples, n, ebn0_db, *prng)` adds noise with q15 saturation.
- A thin end-to-end harness (host + reusable by the app): PRBS → BPSK map → AWGN → slice → BER.

**Host tests**
- σ for a fixed seed/Eb/N0 matches the Python golden within tolerance (validates the variance math).
- **BER vs Eb/N0 matches the theoretical BPSK curve** `BER = Q(√(2·Eb/N0))` within a statistical
  tolerance band at several points (e.g. 0, 2, 4, 6, 8 dB), using enough bits per point that the
  band is tight. This is the headline correctness check of the whole sub-track.

**Validation:** Measured BER within tolerance of `Q(√(2·Eb/N0))` at all tested SNR points; same seed
→ identical BER (determinism).

### Phase B0.3 — `apps/dsp/modem_sim` + CLI + HIL

**Scope**
- `apps/dsp/modem_sim/`: links `lib/prbs`, `lib/modem`, `lib/channel` and the CLI engine.
- CLI commands:
  - `modem run --mod bpsk --snr <dB> --bits <N>` → prints `bits / errors / BER / theory / Mcycles`.
  - `modem sweep --snr <lo>:<hi>:<step>` → prints an ASCII BER-vs-Eb/N0 table/curve over serial.
- DWT cycle counter wraps the per-call processing to report **cycles/bit** (feeds the perf-baseline
  culture already in `tests/baselines/`).
- HIL test (`HIL_TEST=1`) asserts: BER at a fixed SNR/seed is within the theory band, and cycles/bit
  is under a recorded budget (regression guard).

**Validation:** On real hardware, `modem run --snr 6` reports BER ≈ 2.4e-3 within band; HIL test green;
a perf baseline JSON committed under `tests/baselines/`.

### Phase B0.4 — RRC pulse shaping + matched filter (real waveforms)

**Scope**
- `lib/dsp/`: q15 root-raised-cosine FIR (β=0.35, configurable span/SPS), upsample (zero-stuff +
  filter) on TX, matched filter + symbol-instant decimation on RX.
- The channel now operates on **oversampled samples**, not symbols — the seam is unchanged; AWGN just
  runs at sample rate.
- Golden-reference host tests: combined TX+RX RRC = raised-cosine → **ISI-free at Nyquist instants**
  (zero crossings at symbol centres), taps match Python within ±2 LSB.

**Validation:** Nyquist-ISI-free property holds in test; BER with pulse shaping (no noise) is 0; with
noise, still tracks theory (matched filter is information-lossless).

### Phase B0.5 — Synchronisation & impairments ✅ landed (#197)

Shipped as three stacked PRs: (1) complex baseband primitives + impaired channel
(`cq15_t`, sincos LUT, NCO, `lib/channel/impair`), (2) `lib/sync` RX recovery
loops (AGC, M&M timing, Costas, Barker-13), (3) integration into `modem_sim`
behind a `--sync` flag + HIL Tier 9c. The combined-offset frame locks and
recovers at ~1.2× theory BER (6 dB), within the documented 4× ideal-sync bound.

**Scope**
- Channel gains optional **timing offset, carrier frequency offset, phase offset** (still all software).
- RX gains: **AGC** (slow scalar gain), **Mueller-and-Müller timing recovery**, decision-directed /
  Costas-loop phase recovery, and **Barker-13 preamble frame sync**.
- CLI flags expose each impairment so they can be toggled and their cost in BER/lock-time observed.

**Validation:** RX locks and recovers bits under combined timing+phase offset; BER degradation vs the
ideal-sync case is bounded and documented.

### Phase B0.6 — FEC: Hamming(7,4)

**Scope**
- `lib/fec/hamming74.{c,h}` — pure `encode(nibble)→7 bits`, `decode(7 bits)→nibble + corrected?`.
- Modem TX/RX gain a `--fec hamming` toggle.
- Exhaustive host tests over the 16-symbol input space × all single-bit error positions.

**Validation:** Every single-bit error corrected; BER-vs-SNR curve shows the expected coding gain
over uncoded BPSK at BER=1e-3.

## Expansion path (deferred — not part of this sub-track's issues)

Once the software modem is solid, the original Plan 002 hardware tracks reattach as **channel
backends** behind the same seam, in increasing order of hardware cost:

1. `tools/ber_plot.py` — host-side SNR sweep driving `modem sweep` over serial, matplotlib BER curves
   vs theory (the original Phase 2.12 tool, now trivial because the firmware already sweeps).
2. **Single-board analog loopback** — PWM-DAC → RC filter → ADC on the *same* board (original
   Phase 2.8 fixture, minus the second board). The software channel becomes a real (short) wire.
3. **Two-board over-the-wire** — board A TX → board B RX (original Phases 2.1 / 2.8), reusing the
   two-board HIL fixture and `BOARD_NODE=A|B` strapping.
4. **Higher-order / harder channels** — QPSK, flat fading, and convolutional + Viterbi (original
   Phase 2.13) for additional coding gain.

## Risk

- **Fixed-point scaling/overflow** is the main correctness hazard — mitigated by the shared `fixed.h`
  saturating helpers, ±2-LSB golden-vector tests, and validating BER against closed-form theory
  (a wrong scale factor shows up immediately as a BER curve that doesn't match `Q(·)`).
- **PRNG determinism in CI** — the noise generator must be fully deterministic per seed so HIL/host
  results are reproducible; this is a test requirement, not just a nicety.
- **Statistical test flakiness** — BER-vs-theory bands must use enough bits per SNR point that the
  tolerance band is tight but not flaky; document the bit counts and the band derivation.

## Out of scope (this sub-track)

- Any physical fixture (PWM-DAC, RC filter, ADC, second board) — all deferred to the expansion path.
- CMSIS-DSP dependency — hand-rolled kernels first.
- QAM/QPSK, fading channels, LDPC/Turbo — BPSK + AWGN + Hamming is sufficient to stand up the
  methodology end-to-end.
