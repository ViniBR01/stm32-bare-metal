# Plan 002 — Inter-Board Comms + DSP Baseband Track

**Status:** proposed
**Tracking issue:** _(to be filed)_
**Depends on:** [Plan 000 — Repository Refactor](000-repo-refactor.md)
**Optional dependency:** [Plan 001](001-bootloader-and-security.md) — `lib/framing/` is shared; whichever track lands first defines it.

## Why

This track turns the pair of NUCLEO-F411RE boards into a wired comms+DSP testbed. It exercises three competencies:

1. **Inter-board protocol engineering** — implementing UART, SPI, and I²C as fully bidirectional links with framing, retransmit, flow control, and benchmarking.
2. **Real-time DSP on Cortex-M4F** — using CMSIS-DSP, FIR/IIR filters, FFT, and writing a software modem (BPSK or M-FSK) end-to-end.
3. **Measurement methodology** — throughput, latency, jitter, and BER curves vs SNR, with reproducible HIL measurements.

The two boards talk over physical wires — no RF — so all impairments are injected in software. This makes results reproducible and lets us study channel coding (FEC) without an anechoic chamber.

## End-state vision

- Two boards, wired together via headers, each runnable as **node A** or **node B** based on a build-time flag or a strapping pin.
- A reliable framed link layer (`lib/framing/`) reused across UART / SPI / I²C transports.
- Throughput, latency, and jitter benchmarks for each transport, automated and plotted from CSV.
- A software modem (BPSK first, optional M-FSK extension) running over a wired analog link: PWM-DAC out one board → ADC in on the other.
- Forward error correction (Hamming(7,4) baseline, optional convolutional + Viterbi).
- BER vs SNR curves produced by injecting Gaussian noise in software at the receiver.
- Written-up measurement report comparing protocols and modem configurations.

## Hardware requirements

- 2× NUCLEO-F411RE (already on hand).
- Jumper wires + a small breadboard.
- For DSP analog link: 1× RC low-pass filter (10kΩ + 10nF) per board, optionally 1× MCP4921 SPI DAC for higher fidelity (~$2).
- For I²C: 2× 4.7 kΩ pull-up resistors to 3.3 V.
- Logic analyser (optional but useful — Saleae or similar).

## Reusable middleware (`lib/`)

| Library | Purpose |
|---|---|
| `lib/framing/` | HDLC-style framing, CRC-16, ACK/NACK, sequence numbers (shared with Plan 001) |
| `lib/link/` | Transport-agnostic link layer — submit/receive frames, callbacks, stats |
| `lib/dsp/` | FIR, IIR, FFT wrappers around CMSIS-DSP; raised-cosine filter; matched filter |
| `lib/modem/` | Symbol mapper/demapper, frame sync, timing recovery, AGC |
| `lib/fec/` | Hamming(7,4); optional Viterbi for K=7 convolutional code |
| `lib/channel/` | AWGN noise injector (host + on-target) for BER experiments |

## Apps (`apps/comms/`, `apps/dsp/`)

| App | Role | Purpose |
|---|---|---|
| `comms/uart_pingpong` | A & B | Latency / throughput baseline over UART |
| `comms/spi_pingpong` | A=master, B=slave | Same, over SPI; requires SPI slave driver |
| `comms/i2c_pingpong` | A=master, B=slave | Same, over I²C; requires I²C master + slave drivers |
| `comms/bench_runner` | A | Runs a sweep across protocols and rates, emits CSV over UART |
| `dsp/modem_tx` | A | Generate symbols, pulse-shape, output via PWM-DAC |
| `dsp/modem_rx` | B | Sample via ADC+DMA, AGC, matched filter, sync, demap |
| `dsp/loopback_test` | single board | Self-test with internal connection (sanity check before wiring boards) |

## Host tools (`tools/`)

| Tool | Purpose |
|---|---|
| `bench_plot.py` | Read CSV from `bench_runner`, plot throughput/latency/jitter |
| `ber_plot.py` | Sweep SNR, collect BER samples, plot curves |
| `iq_dump.py` | Receive on-target IQ snapshots over UART, plot constellation |

## Phases

Each phase is one issue / one PR. Phases assume Plan 000 has landed.

---

### Sub-track A — Inter-board comms

#### Phase 2.1 — Two-board test fixture

**Scope:**
- Document wiring on the wiki: which pins, which jumpers, ground-loop avoidance
- Build-time macro `BOARD_NODE=A|B` plumbed through `Makefile.common`
- Both boards run the same firmware image with role chosen at boot from a strapping pin (USER button held at reset = node B)
- Update HIL infrastructure: `run_hil_tests.py` learns to flash either board; `mcp_hil_server.py` gains a `board` parameter
- A trivial app that prints its role to UART, used to verify the fixture

**Validation:** Both boards report correct role from independent serial captures.

#### Phase 2.2 — `lib/framing/` reliable frame layer

**Scope:**
- HDLC-style byte-stuffing (0x7E flag, 0x7D escape)
- CRC-16-CCITT (reuse hardware CRC driver where available)
- ACK/NACK with sequence numbers, sliding window of 1
- Timeout + retransmit, configurable per transport
- Pure-host unit tests for the encoder/decoder

**Validation:** Host tests cover frame round-trip, corruption, lost ACK, duplicate detection. ≥95% line coverage.

#### Phase 2.3 — UART ping-pong app + benchmark

**Scope:**
- `apps/comms/uart_pingpong`: sends N-byte frame, awaits echo, measures RTT
- Sweep frame size and baud rate
- Emit CSV: `transport,baud,frame_size,throughput_kbps,rtt_us_p50,rtt_us_p99,error_count`
- HIL test asserts throughput and error rate are within tolerance

**Validation:** RTT < 5 ms at 115200 baud / 64-byte frames. Zero CRC errors over 1M frames.

#### Phase 2.4 — SPI slave driver + ping-pong

**Scope:**
- Extend the SPI driver: slave mode with NSS hardware control + RX/TX DMA
- New driver host tests for slave configuration paths
- `apps/comms/spi_pingpong`: A=master clocks the link; B=slave responds
- Compare achievable throughput at 1, 5, 10 MHz SPI clock

**Validation:** SPI clock up to 10 MHz with no CRC errors at the framing layer. CSV output matches UART format.

#### Phase 2.5 — I²C master + slave driver (closes Issue #66)

**Scope:**
- Implement I²C master: 7-bit addressing, repeated-start, polled and DMA flavours
- Implement I²C slave: address-match interrupt, RX/TX DMA, clock-stretching
- Full host tests for both
- `apps/comms/i2c_pingpong` at 100 kHz, 400 kHz, 1 MHz (Fast Mode Plus if pads support it)

**Validation:** 1 MHz I²C with pull-ups; CRC errors zero over 1M frames. CSV emitted.

#### Phase 2.6 — Bench runner + plotting

**Scope:**
- `apps/comms/bench_runner`: sweeps all three transports automatically, dumps CSV
- `tools/bench_plot.py`: matplotlib plots — throughput vs payload size, latency CDFs
- Wiki page summarising findings

**Validation:** Benchmark report committed under `docs/wiki/plans/002-comms/results-comms.md`.

---

### Sub-track B — DSP baseband

#### Phase 2.7 — Vendor CMSIS-DSP, basic filters

**Scope:**
- Add CMSIS-DSP as a 3rd-party submodule
- `lib/dsp/`: thin wrappers for `arm_fir_f32`, `arm_biquad_cascade_df1_f32`, `arm_rfft_fast_f32`
- Host tests with reference signals (impulse, sinewave, chirp) and Python-generated golden outputs
- A loopback app that runs an FIR and reports cycles/sample using the cycle counter

**Validation:** Filter outputs match reference within 1e-5 relative error. Cycle counts logged as baseline.

#### Phase 2.8 — Analog link fixture

**Scope:**
- Wire PWM output of board A through an RC low-pass filter into the ADC input of board B (and vice versa for full-duplex)
- Document the filter cutoff vs PWM carrier choice
- Self-test app: board A outputs a sinewave via PWM-DAC, board B captures via ADC+DMA, sends samples back over UART
- `tools/iq_dump.py` plots the captured waveform vs the original

**Validation:** Captured sine matches generated sine in frequency and amplitude (with documented attenuation).

Optional upgrade later: replace PWM+RC with an MCP4921 SPI DAC for cleaner output.

#### Phase 2.9 — `lib/modem/` BPSK transmit

**Scope:**
- Bit → symbol mapper (BPSK: 0→-1, 1→+1)
- Root-raised-cosine pulse shaping (β=0.35), upsample
- Buffer + DMA out via PWM (or SPI DAC if installed)
- `apps/dsp/modem_tx`: transmit a known PRBS sequence at chosen symbol rate
- Pure host tests for the symbol mapper and pulse-shaping kernel against Python references

**Validation:** Transmit waveform matches Python-generated reference (captured by `iq_dump.py`).

#### Phase 2.10 — `lib/modem/` BPSK receive

**Scope:**
- ADC+DMA capture
- AGC (slow-loop scalar gain)
- Matched filter (root-raised-cosine, mirrored)
- Mueller-and-Müller timing recovery
- Frame sync via known preamble (Barker-13 or m-sequence)
- Symbol slicer; demap to bits
- `apps/dsp/modem_rx`: locks to TX, reports BER over a known PRBS

**Validation:** BER < 1e-4 with no injected noise on the wired link.

#### Phase 2.11 — FEC: Hamming(7,4)

**Scope:**
- `lib/fec/hamming74.{c,h}` — pure functions, encode + decode-with-correct
- Host tests with exhaustive 4-bit input space + single-bit error injection
- Modem TX/RX wired to use FEC; toggle on/off via command-line flag

**Validation:** With FEC on, single-bit errors corrected. BER drops as expected.

#### Phase 2.12 — Channel model + BER curves

**Scope:**
- `lib/channel/awgn.c` — add Gaussian noise to received samples in software, controlled SNR
- `dsp/modem_rx` gains a noise-injection mode (post-ADC, pre-AGC)
- `tools/ber_plot.py` sweeps SNR, runs N frames per point, emits BER vs SNR plot
- Compare measured curve to theoretical BPSK + Hamming(7,4) curve

**Validation:** Measured BER curve within ~1 dB of theory at BER=1e-3.

#### Phase 2.13 — (Optional) Convolutional + Viterbi

**Scope:**
- K=7, rate-1/2 convolutional encoder
- Soft-decision Viterbi decoder (CMSIS-DSP fixed point)
- BER curve comparison: uncoded vs Hamming vs Viterbi
- Document on-target decode throughput (samples/sec)

**Validation:** Viterbi recovers ~5 dB coding gain at BER=1e-4 vs uncoded.

#### Phase 2.14 — Measurement report

**Scope:**
- `docs/wiki/plans/002-dsp/results-dsp.md` — final write-up
- Plots: TX waveform, RX constellation (1-D for BPSK), BER curves uncoded vs FEC variants
- Honest tradeoff discussion: PWM-DAC vs SPI-DAC, where the implementation hits Cortex-M4 limits

**Validation:** Doc review; cross-link from architecture page.

---

## Risk & rollback

- **SPI/I²C slave drivers** are new territory — slave mode is materially more complex than master (clock stretching, NSS edge cases). Allocate buffer for surprises in Phases 2.4 and 2.5.
- **PWM-DAC quality** may limit modem performance. The plan accepts this; if it bottlenecks the BER experiments, swap to MCP4921 — covered as an optional upgrade in Phase 2.8.
- **CMSIS-DSP size** is significant. Confirm flash budget early; LTO + dead-code elimination should keep only used kernels.
- **Two-board HIL** complicates the runner. Plan 2.1 must fully resolve flash-and-test for both boards before any later phase relies on it.
- **Wiring stability** — bad connections will look like protocol bugs. Document a "wiring sanity check" first-thing-to-do for any debug session.

## Out of scope

- Wireless RF — wired only. No SDR, no RF frontend, no antenna.
- QAM beyond QPSK — BPSK first, M-FSK or QPSK as optional extensions.
- LDPC, Turbo codes — Hamming and Viterbi are sufficient to teach the methodology.
- Bluetooth / WiFi / LoRa via external modules — that's a different project.
