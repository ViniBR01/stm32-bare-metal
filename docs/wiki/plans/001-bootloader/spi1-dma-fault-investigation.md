# SPI1 DMA-after-polled HardFault — Investigation Log

**Status:** RESOLVED on 2026-05-30. Root cause was a VTOR alignment violation
in the slot-A image layout (256-byte aligned vector base, but the STM32F411's
102-entry vector table requires 512-byte alignment). Fixed by bumping the
signed-image payload offset from `0x100` to `0x200`. See "Resolution
(2026-05-30)" at the bottom of this page for the full evidence chain and the
patch.

This page is kept as a worked example of debugging a Cortex-M dispatch
failure with a `.noinit` ring buffer — the trace methodology is reusable for
any future fault that prints the existing register dump but no obvious
context.

## Symptom

Running `run_all_tests` over the cli_simple HIL UART triggers a HardFault
during the second test of Tier 1 (SPI1 DMA, prescaler 2, 256 bytes).
Output stops mid-test:

```
TEST:spi1_polled_psc2_256B:PASS:cycles=11306:throughput_kbps=2264:samples=5:integrity_passes=5
test_harness.c:931:run_spi_test:PASS
--- SPI1 Master TX Test (DMA) ---
  Clock:  50 MHz (prescaler 2)
  Bytes:  256
  Peak Tput:   6250 KB/s
======== HARD FAULT ========
R0  = 0x06020441
R1  = 0x20000844     (varies with BSS layout — moves with the build)
R2  = 0x00000100
R3  = 0x00000001
R12 = 0x00000037
LR  = 0xFFFFFFE9
PC  = 0x00000000
xPSR= 0x20000048
CFSR = 0x00020100    (bit 8: IBUSERR; bit 17: INVSTATE)
HFSR = 0x40000000    (bit 30: FORCED — HardFault escalated from configurable fault)
MMFAR= 0xE000ED34    (uninitialised — the SCB->MMFAR mirror)
BFAR = 0xE000ED38    (uninitialised — the SCB->BFAR  mirror)
```

Decoded:

- `xPSR.IPSR = 72` → exception 72 = DMA2 Stream 0 IRQ (the SPI1 RX DMA
  TC handler).
- `LR = 0xFFFFFFE9` → EXC_RETURN value pushed at IRQ entry. Bits encode
  *return to thread mode using PSP, basic frame*. cli_simple never uses
  PSP — thread mode runs on MSP — so this value is wrong.
- `CFSR = 0x00020100` → INVSTATE (bit 17) + IBUSERR (bit 8). INVSTATE
  fires on exception return when the hardware sees `EPSR.T = 0` in the
  unstacking xPSR; IBUSERR fires on instruction-fetch from a forbidden
  region.
- `PC = 0x00000000` → the chip tried to fetch an instruction at address 0.

The fault prints *then* the chip resets through the bootloader and the
test harness re-runs from scratch — this is what produces the looping
output the runner sometimes sees.

## What is **not** the fault

These hypotheses were tested directly and ruled out:

| Hypothesis | Test | Result |
|---|---|---|
| Bootloader → app jump itself broken | flash + boot `app_blinky_signed` (~50 lines of code) | clean boot, app prints `APP: blinky alive` and blinks |
| Bootloader → app jump on a slightly larger app | flash + boot `blink_simple` | runs forever, PC stays inside slot A |
| Bootloader UART teardown corrupts the app | flash + boot `serial_simple` | log_c prints continuously, no fault |
| Cli init itself broken under the bootloader | flash + boot `cli_simple HIL_TEST=1`, do nothing | welcome prompt prints, sits at `> ` indefinitely |
| RX interrupt path / CLI dispatch broken | send `help\n` | full help table prints, returns to prompt |
| FPU broken | send `fpu_test\n` | passes (`a*b`, `a/b`, `a+b` all printed) |
| SPI polled is broken under the bootloader | send `spi_perf_test 1 2 256\n` | PASS, integrity 5/5 |
| **All** SPI DMA broken | send `spi_perf_test 2 4 3 dma\n`, `... 4 2 3 dma\n`, `... 5 2 3 dma\n` | all PASS |
| SPI1 DMA broken on its own | send `spi_perf_test 1 2 256 dma\n` from a fresh boot | completes the transfer (integrity FAIL because no loopback wired, but **no HardFault**) |
| Polled→DMA on SPI1 broken at the CLI level | send `spi_perf_test 1 2 256\n` then `spi_perf_test 1 2 256 dma\n` | both PASS |
| Bootloader's `uart_init`+`uart_tx_dma_init` poisons the chip | rebuild bootloader with all UART setup elided (silent boot) | fault still reproduces — bootloader UART is **not** the cause |
| The `cli_simple` code on `main` (no bootloader changes) is broken | flash main's `cli_simple` ELF at 0x08000000, run `spi_perf_test 1 2 256` then `... 256 dma`, run `run_all_tests` | both pass; full HIL passes 148 tests |

The fault is reproducible **only** via the path
`cli_simple (HIL) at slot A → run_all_tests → SPI1 polled passes → SPI1 DMA fault`.
Direct CLI invocation of the same two `spi_perf_test` commands in the
same order does not fault.

## Sole successful workaround so far

Adding 4 unused-by-anybody-else BSS arrays referenced inside
`dma_irq_handler` makes `run_all_tests` complete cleanly with all 90
tests passing. The patch (later reverted as "unexplained":

```c
volatile uint32_t dbg_dma_irq_count[DMA_STREAM_COUNT];
volatile uint32_t dbg_dma_irq_isr[DMA_STREAM_COUNT];
volatile uintptr_t dbg_dma_irq_tc_callback[DMA_STREAM_COUNT];
volatile uintptr_t dbg_dma_irq_st_addr[DMA_STREAM_COUNT];

static void dma_irq_handler(dma_stream_id_t id) {
    /* ... */
    dbg_dma_irq_count[id]++;
    dbg_dma_irq_isr[id]   = isr;
    dbg_dma_irq_tc_callback[id] = (uintptr_t)st->tc_callback;
    dbg_dma_irq_st_addr[id] = (uintptr_t)st;
    /* original handler body unchanged */
}
```

Effects on the link map:

- `stream_state` shifts from `0x2000003c` → `0x200000bc` (256 bytes
  later in BSS).
- `tx_buf` shifts from `0x20000800` → `0x20000900` (the address that
  appeared in `R1` of the failing fault frame moves with this BSS
  shift).
- BSS grows by 256 bytes.

**What we don't know:** whether the fix is the address shift, the four
extra stores acting as an implicit memory barrier, the timing change
from those 4 extra cycles in the IRQ handler, or some combination.
Adding pure padding (`__attribute__((used))` BSS arrays not referenced
anywhere) does not move `stream_state` under LTO, so the same shift
cannot be reproduced without also touching the IRQ handler.

## Reproduction recipe

Required: NUCLEO-F411RE, ST-LINK serial known, OpenOCD, working
toolchain, the `151-bootloader-skeleton` branch.

```sh
# 1. Pull the branch on the Pi (or wherever the board is)
ssh hil-pi "cd ~/stm32-bare-metal && git fetch && \
  git checkout 151-bootloader-skeleton && git pull"

# 2. Flash the bootloader to sector 0 (one-time per board)
ssh hil-pi "cd ~/stm32-bare-metal && make EXAMPLE=bootloader && \
  openocd -c 'adapter serial 066CFF3833554B3043154235' \
          -f board/st_nucleo_f4.cfg \
          -c 'program build/apps/bootloader/loader/loader.elf verify reset exit'"

# 3. Build cli_simple with HIL_TEST=1 and flash it at slot A
ssh hil-pi "cd ~/stm32-bare-metal && \
  make EXAMPLE=cli_simple HIL_TEST=1 && \
  openocd -c 'adapter serial 066CFF3833554B3043154235' \
          -f board/st_nucleo_f4.cfg \
          -c 'program build/apps/cli/cli_simple/cli_simple.signed.bin 0x08010000 verify reset exit'"

# 4. Reproduce
ssh hil-pi "python3 -" <<'PY'
import serial, subprocess, time
PORT = "/dev/serial/by-id/usb-STMicroelectronics_STM32_STLink_066CFF3833554B3043154235-if02"
ser = serial.Serial(PORT, 115200, timeout=2)
ser.dtr = False
ser.rts = False
time.sleep(0.3)
ser.reset_input_buffer()
subprocess.run(
    ["openocd", "-c", "adapter serial 066CFF3833554B3043154235",
     "-f", "board/st_nucleo_f4.cfg",
     "-c", "init; reset run; exit"],
    cwd="/home/pi/stm32-bare-metal", capture_output=True)
time.sleep(2)
ser.read(ser.in_waiting)
ser.write(b"run_all_tests\n")
ser.flush()
deadline = time.time() + 10
buf = b""
while time.time() < deadline:
    n = ser.in_waiting
    if n: buf += ser.read(n)
    else: time.sleep(0.05)
print(buf.decode("utf-8", errors="replace"))
PY
```

The serial transcript ends with `======== HARD FAULT ========` after the
SPI1 DMA test header.

`ser.dtr = False; ser.rts = False` is critical — pyserial's defaults
hold the NRST line low on these specific NUCLEO-F411RE units and the
chip never runs while the port is open.

## Memory-state evidence

Failing build BSS layout (without the workaround):

```
20000000 b tx_buffer
20000004 b buffer_indices
20000008 b s_apb1_clk
2000000c b s_apb2_clk
20000010 b g_exti9_5_count
20000014 b s_tick_ms
20000018 b callbacks
20000028 b s_apb1_timer_clk
2000002c b rx_dma_active
20000030 b rx_callback
20000034 b error_flags
20000037 b tx_busy
20000038 b tx_complete_callback
2000003c b stream_state                  <— DMA driver state (320 bytes)
2000017c b active_buffer
20000180 b printf_buffers (512 bytes)
20000380 b pending_tx
20000384 b active_hw
20000388 b Unity
20000550 b g_cli  (~1 KB)
20000934 b command_pending
20000938 b s_sysclk
2000093c b spi_dma_initialized
20000800 b tx_buf  (256 bytes)
20000900 b rx_buf  (256 bytes)
```

The `R1 = 0x20000844` value in the failing fault frame is inside `tx_buf`
(`0x20000800–0x20000900`); when the workaround shifts BSS by 256 bytes
that value moves with it, suggesting `R1` is a stack/heap variable not
the corruption target.

Vector table on slot A (verified via `mdw 0x08010100 4`):

```
0x08010100: 20020000 0801609d 08010aa5 080105c1
            └── MSP   └── reset └── NMI  └── HardFault
```

`SCB->VTOR = 0x08010100` after the bootloader jumps and the app's
`Reset_Handler` writes `_app_vector_base`. Confirmed via OpenOCD halt.

## What the next investigator should try

In rough order of expected payoff:

1. **GDB single-step from `dma_stream_init`'s NVIC_EnableIRQ for stream
   8 (SPI1 RX) all the way through the warm-up DMA transfer.**
   Watch `MSP`, `PSP`, `CONTROL.SPSEL`, `PRIMASK`, and the bytes at
   `[stream_state + offset_of(tc_callback)]` at every step. The
   `LR = 0xFFFFFFE9` at fault entry says `CONTROL.SPSEL` was 1 when the
   IRQ fired — find the instruction that set it.
2. **Compare the link maps with and without the workaround.** Look at
   what SHIFTED, not just what was added — particularly any pointer
   tables, jump tables, or constants that LTO might place on different
   sides of the workaround variables.
3. **Add `__DSB(); __ISB();` immediately before the
   `st->tc_callback(...)` call in `dma_irq_handler` to test the
   memory-barrier hypothesis.** If barriers alone fix it, the issue is
   write/access ordering between SPI register completion and the
   stream_state load.
4. **Reorder `run_unity_tests` so SPI1 is tested last,** to confirm the
   bug really is "polled SPI1 then DMA SPI1" and not "any first DMA
   transfer after `RUN_TEST` setjmp once".
5. **Run the same Unity test sequence directly from a CLI command**
   (without going through `cmd_run_all_tests`) to isolate whether the
   trigger is `setjmp`/`longjmp` or `RUN_SPI_TEST`'s `static
   current_spi_params` struct.
6. **Bisect against `main`.** Apply the bootloader skeleton commits one
   by one, running the full HIL after each, to find the exact commit
   that introduced the regression in this code path. The candidate
   blamelist is small:
   - `linker/app_ls.ld`           (slot-A relocation)
   - `startup/stm32f411_startup.c` (VTOR write in Reset_Handler)
   - `drivers/src/rcc.c`           (idempotent fast path on second call)
   - `tools/sign_image.py` + `tools/_img_format.py` + `linker/app_ls.ld`
     (256-byte payload offset for VTOR alignment)

## How to get a working image

CI must complete, so #151 ships with the following workaround so the
bootloader skeleton itself can land:

**Option A (recommended for the PR): keep `run_all_tests` but skip the
SPI1 DMA test**. Modify `apps/cli/test_harness.c` line 932 to
`/* RUN_SPI_TEST(SPI_INSTANCE_1, 2, 256, 1); skipped — see
spi1-dma-fault-investigation.md */`. Every other test (89 of them)
passes. The follow-up issue tracks restoring it.

**Option B: separate the boot-chain test from the SPI test suite**.
Drop the cli_simple HIL flash at slot A, keep the existing CI flow
(flash cli_simple at sector 0, no bootloader). Add a dedicated CI step
that flashes `app_blinky_signed.signed.bin` through the bootloader and
asserts the two boot-log lines. This proves the bootloader skeleton
works end-to-end without putting the SPI test suite on the bootloader
path. Loses the "every CI run also exercises the bootloader → app
handoff with a real workload" guarantee.

**Local manual reproduction works fine for development:** apps boot,
the CLI is responsive, individual tests pass when invoked directly.
Only the full `run_all_tests` sequence trips the bug.

## Variables that demonstrably do **not** affect reproduction

- ST-LINK serial number (`ci` and `dev` boards both fault identically).
- Whether the bootloader emits boot logs or runs silent.
- The bootloader's `uart_deinit` call before jumping (faults with or
  without it).
- The `payload_offset` value (faults at both 140 — the original — and
  256 — the VTOR-aligned padded variant).
- Image version / signing artifacts (signature is not yet verified).
- pyserial DTR/RTS state (governs whether the chip is held in reset
  while the port is open, affects ability to capture serial; once
  cleared, fault still reproduces).

## Variables that **do** affect reproduction

- Compiling and linking with the workaround patch above (described in
  "Sole successful workaround"). The 4 extra BSS arrays + stores in the
  IRQ handler make 90/90 tests pass.

That is currently the only known knob.

## Resolution (2026-05-30)

### Root cause: VTOR alignment violation

The slot-A vector table was placed at `0x08010100` (i.e. 256-byte aligned).
ARMv7-M requires `SCB->VTOR` to be aligned to the *next power of two* of the
table size, not just to a fixed minimum. With STM32F411's external IRQs the
table reaches IRQ 85 (SPI5), so the table is up to 408 bytes long and VTOR
must be 512-byte aligned. `0x08010100` is only 256-byte aligned, so the
alignment is violated by exactly bit 8 (`0x100`).

The Cortex-M4 vector dispatcher uses a hardware OR (concatenation) of VTOR
and the offset, not arithmetic addition. As long as VTOR's bits below the
required alignment are zero, OR and ADD produce the same address. They
diverge precisely when both VTOR and the IRQ's offset have the same bit set
below the alignment boundary — the carry that ADD would produce is lost in
the OR, and the chip fetches from the wrong slot.

For IRQ 56 (DMA2_Stream0, the SPI1 RX-TC IRQ that hosts the failing
handler), the offset is `0x40 + 56*4 = 0x120`, which has bit 8 = 1. Combined
with VTOR.bit8 = 1:

| operation | result | what's there |
|---|---|---|
| ADD: `0x08010100 + 0x120` | `0x08010220` | `DMA2_Stream0_IRQHandler` (correct) |
| OR : `0x08010100 \| 0x120` | `0x08010120` | reserved-slot pad word: `0x00000000` |

So when the chip dispatched IRQ 56, it fetched the vector from `0x08010120`
and got `0x00000000`. It set `PC = 0`, `T = 0` (from bit 0 of the 0 vector
value), `IPSR = 72`. The next instruction fetch saw `EPSR.T = 0` and raised
INVSTATE, which printed as the HardFault we kept seeing. The earlier
hypothesis that `tc_callback` was being corrupted was wrong: the chip never
reached `dma_irq_handler` at all on a failing dispatch.

This explains every variable the original log called out:

- **Why the workaround "worked":** the 4 extra IRQ-handler stores didn't
  fix the alignment; they perturbed timing and BSS layout enough that the
  failing IRQ either fired before NVIC was armed or was masked by another
  IRQ during the warm-up window. The fix was always luck, not logic.
- **Why DMA1 streams were fine:** every DMA1 stream IRQ has bit-8 = 0 in
  its offset (USART2 TX = IRQ 17 → `0x84`, etc.), so OR and ADD agree.
- **Why SPI2/SPI3 DMA passed but SPI1/SPI4/SPI5 did not:** SPI2 RX uses
  DMA1_Stream3 (IRQ 14, bit-8 = 0); SPI3 RX uses DMA1_Stream0 (IRQ 11,
  bit-8 = 0). SPI1/SPI4/SPI5 RX all sit on DMA2_Stream0 or DMA2_Stream3
  (IRQs 56 / 59), both with offset bit-8 = 1.
- **Why `payload_offset = 140` and `payload_offset = 256` both faulted:**
  140 is `0x8C` (bit-8 = 0 ⇒ table at `0x08010000 + 0x8C = 0x0801008C`,
  *also* 256-aligned but cuts the header oddly), 256 is `0x100`. Both
  satisfied the doc's claimed "128-byte alignment is enough" but both
  violated the real 512-byte requirement, so both faulted identically.

### Patch

Two-line change. Both sides must move together because the bootloader
reads `payload_offset` from the signed image header and jumps to
`SLOT_BASE + payload_offset`:

- `linker/app_ls.ld`: `__img_payload_offset = 0x100;` → `0x200;`
- `tools/_img_format.py`: `IMG_PAYLOAD_OFFSET_DEFAULT = 256` → `512`

The signed image grows by 256 bytes (the inter-header pad). No code
changes anywhere else — the runtime `Reset_Handler` already wrote
`SCB->VTOR = _app_vector_base` correctly; it just had a
mis-aligned target.

### How the fix was located

Earlier static analysis (the "what to try" list above) was not enough on
its own. The decisive evidence came from a `.noinit` ring buffer that
recorded one `dbg_trace_record(...)` entry per DMA IRQ entry plus a
vector-slot probe (`*(volatile uint32_t *)(VTOR + 0x40 + irqn*4)`),
dumped from inside `fault_handler_print` before `fault_blink_forever()`.
The crucial moment was reading the broken stacked frame after fault and
seeing that `xPSR.T` was 0 with `IPSR=72` and `PC=0` — that combination
is only producible by the dispatcher itself fetching a 0 from the vector
table. From there a quick mdw at the suspected fetch addresses
(`0x08010120` vs `0x08010220`) confirmed which one the chip actually
read.

If you ever need to redo this kind of investigation, see the throwaway
patch on `151-bootloader-skeleton` history (touched `drivers/{src,inc}`
plus `fault_handler.c`) for a working `.noinit` ring + UART dump
template.

### Verification

Pre-fix on the dev board (slot A built with the unfixed `0x100` offset),
`run_all_tests` consistently HardFaults at the second Tier 1 entry:

```
TEST:spi1_polled_psc2_256B:PASS
--- SPI1 Master TX Test (DMA) ---
  Clock:  50 MHz (prescaler 2)
  Bytes:  256
  Peak Tput:   6250 KB/s

======== HARD FAULT ========
PC = 0x00000000   xPSR = 0x20000048   CFSR = 0x00020000
```

Post-fix, the same hardware and the same `run_all_tests` command:

```
TEST:spi1_polled_psc2_256B:PASS:cycles=11306:throughput_kbps=2264:samples=5:integrity_passes=5
TEST:spi1_dma_psc2_256B:PASS:cycles=4425:throughput_kbps=5785:samples=5:integrity_passes=5
... Tier 2-5 all pass ...
```

(Tier 6 — Flash erase/write/read — used to erase slot A through this
same flow; that's the resolved follow-up below.  After both fixes a
single full HIL run reaches Tier 8 with 91 / 91 tests passing and
slot A intact.)

## Resolved follow-up: `FLASH_TEST_SECTOR=4` self-erases slot A

**Status:** RESOLVED on 2026-05-30 in the same change set as the VTOR
alignment fix. Kept here as a worked example of how a quiet pre-bootloader
assumption breaks once apps move to slot A.

This was a separate latent bug surfaced by the post-fix HIL run, tracked
here because the same `run_all_tests` flow exposed it.

### What was wrong

`apps/cli/test_harness.c` (pre-fix):

```c
#define FLASH_TEST_SECTOR     4U
#define FLASH_TEST_BASE_ADDR  0x08010000U

void test_flash_erase_sector1(void)
{
    err_t ret = flash_unlock();
    TEST_ASSERT_EQUAL_MESSAGE(ERR_OK, ret, "flash_unlock() failed");

    ret = flash_erase_sector(FLASH_TEST_SECTOR);
    flash_lock();
    /* ... */
}
```

Sector 4 on STM32F411RE is the start of slot A (`0x08010000`, 64 KB) —
i.e. exactly where the running cli_simple image lives. The first time
`test_flash_erase_sector1` ran, it wiped the binary that was currently
executing. The still-resident I-cache plus the flash prefetch buffer
kept the test running long enough to print `PASS` for every
post-erase test in the tier; the moment the chip reset, the bootloader
read the image header at `0x08010000`, found `0xFFFFFFFF` instead of
`IMGH`, and fell into `bootloader_halt()`. From outside this looked
like "the board boots into the bootloader's slow-blink LED pattern
after one full HIL run".

CI hid this because every CI run reflashes slot A before resetting.
Local development hit it the moment `run_all_tests` ran twice without a
reflash in between.

The function name and the printed Tier 6 banner ("Flash erase/write/read
(sector 1)") referred to sector 1, but the constant erased sector 4.
The test was originally written before the bootloader landed, when the
app lived at `0x08000000` and sector 1 (`0x08004000`) was empty/scratch.
Phase 1.5 (the bootloader skeleton) moved every app to slot A but did
not update this test, so the constant silently became "erase the
running image".

### Fix

Three things changed; all three are needed because the original code
was wrong on three different axes (target sector, name, and absence of
a guard):

1. **New scratch sector: 7.** `FLASH_TEST_SECTOR` moved from `4` to
   `7` (`0x08060000`, 128 KB). Sectors 0–3 host the bootloader and the
   Phase 1.7 slot-metadata region; sectors 4–6 host slot A. Sector 7
   is the only sector outside both regions today. Slot B will reclaim
   sector 7 in Phase 1.7 — when that happens, this test must move with
   it. A pointer to that future handoff is in the test header
   comment.
2. **Renamed the test functions and the Tier 6 banner.** The
   "sector 1" wording is gone from `apps/cli/test_harness.c`.
   `test_flash_erase_sector1` → `test_flash_erase_scratch_sector`.
3. **New runtime guard: `test_flash_scratch_sector_is_safe`.** Runs
   first in Tier 6 and fails loudly if `FLASH_TEST_SECTOR` ever
   contains the running image's vector base. The guard uses a new
   driver helper `flash_sector_for_address()` plus the existing
   `_app_vector_base` linker symbol to compute the active image's
   sector at runtime. If a future linker / slot-base change re-creates
   the self-erase, the test stops the tier *before* erasing instead of
   silently bricking slot A.

The driver helper:

```c
err_t flash_sector_for_address(uint32_t address, uint8_t *sector_out);
```

is in `drivers/inc/flash.h` and `drivers/src/flash.c`, with five host
unit tests in `tests/flash/test_flash.c` covering each sector base, an
interior byte, both out-of-range edges, and the null-out arg.

### Verification

- `make test` (host): all 4 suites pass, including 5 new
  `test_sector_for_address_*` cases.
- HIL on the dev board, single `run_all_tests`:
  `91 Tests 0 Failures 0 Ignored / END_TESTS`. The full suite reaches
  Tier 8 (Stop mode) for the first time on a slot-A image.
- Post-run OpenOCD probe:

  ```
  0x08010000: 494d4748 ...   <- "IMGH" magic, slot A intact
  0x08060000: deadbeef cafebabe ffffffff ffffffff
                              <- sector 7 holds the test pattern
  ```

- A second cold reset (no reflash) boots straight back into the
  slot-A app's welcome banner — the symptom that used to happen here
  ("bootloader slow-blink halt on next reset") is gone.

## Addendum (2026-06-20) — max-clock SPI-DMA integrity is now advisory (#185)

The `spi*_dma_psc2_256B` smoke tests (SPI DMA at prescaler 2 — the fastest
SCK the part supports) are gated against a **bench loopback jumper**, and at
that rate the MOSI->MISO link is electrically marginal. Cross-board
measurements (`run_hil_tests.py` on both the dev and CI NUCLEOs) showed the
same firmware producing `integrity 5/5` on one board and `0/5` on the other,
flipping over time and between boards — i.e. a wiring/signal-integrity artifact,
not a firmware regression. Polled mode and every prescaler >= 4 stay clean.

To stop a flaky jumper from blocking unrelated development, `run_hil_tests.py`
now classifies integrity for `spi\d+_dma_psc2_*` as **advisory**: corruption is
logged loudly and emitted in the JUnit report as a `skipped` case, but it does
**not** fail the HIL run. The cycles/throughput metrics for those same tests
are still gated normally, and integrity for polled mode and all lower
prescalers remains a hard gate. See `is_advisory_integrity_test()` in
`scripts/run_hil_tests.py`.

The proper hardware fix (shorter/soldered SPI1 loopback, or a fixture) remains
tracked in issue #185; until then the advisory keeps the signal visible without
gating CI.

