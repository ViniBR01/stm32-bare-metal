# OTA over UART (Plan 001 Phase 1.8)

This page documents the bootloader's OTA receiver path: how the running
app hands control back to the bootloader, what protocol the host
streams the new image over, and how the active-slot swap stays
power-cut safe.

Builds on:
- [verify-and-jump.md](verify-and-jump.md) — Phase 1.6's SHA-256 + ECDSA
  verify path is reused unchanged for the OTA-target slot.
- [ab-slots.md](ab-slots.md) — the active-slot decision tree resolves
  the brief mid-OTA window where both slots' `active` bits are set.

## Pieces

| Component | Lives in | What it does |
|---|---|---|
| `ota_request` CLI command | `apps/cli/cli_commands.c` | Writes `RTC_BACKUP_OTA_MAGIC` (0x4F544131) into RTC backup register 0, then triggers `NVIC_SystemReset()`. |
| RTC backup driver | `drivers/{inc,src}/rtc_backup.{h,c}` | Three-function driver: enable backup-domain writes, read DR0, write DR0. ~30 LOC. |
| Bootloader OTA hook | `apps/bootloader/loader/main.c` | First thing the bootloader does is read DR0; if it equals the magic, clear it and jump into `bootloader_ota_run()`. |
| OTA receiver state machine | `apps/bootloader/loader/ota.c` | UART → framing decoder → state machine. Drives `flash_slot_erase`, `flash_write_bytes`, `verify_slot`, `flash_slot_commit_metadata`. |
| `lib/framing/` | landed in PR #163 | HDLC-style frame layer + CRC-16 + sliding-window-1 ARQ. |
| `tools/ota_send.py` | `tools/` | Host driver. Streams a `.signed.bin` over the framing protocol. |
| `scripts/run_ota_test.py` | `scripts/` | HIL test: clean OTA + tampered OTA. |

## Wire protocol

Built on top of `lib/framing/`. Each line below is one framed packet
on the wire (the framing layer wraps it with FLAG bytes + CRC-16):

| Direction | TYPE | Payload |
|---|---|---|
| host → device | `PING` | _empty_ |
| device → host | `PONG` | _empty_ |
| host → device | `OTA_BEGIN` | `u8 slot_id`, `u32 total_size` (LE) |
| device → host | `ACK` / `NACK` | _empty_ |
| host → device | `OTA_CHUNK` | `u32 offset` (LE), `u8[N]` data, N up to 1020 |
| device → host | `ACK` / `NACK` | _empty_ |
| host → device | `OTA_END` | _empty_ |
| device → host | `STATUS` | `u8 ota_status_t` |

`ota_status_t` is shared with the C side (`apps/bootloader/loader/ota.h`
and `tools/_framing.py`):

| Code | Name | Meaning |
|---|---|---|
| 0 | `ok` | Verify passed, metadata committed, chip resetting. |
| 1 | `verify_failed` | Bytes landed but the SHA/ECDSA check rejected them. Active slot **untouched**. |
| 2 | `write_failed` | Flash write or metadata commit returned an error. |
| 3 | `protocol_error` | OTA_END seen before BEGIN, or some other state-machine violation. |

Maximum frame payload is 1024 bytes (set by `FRAME_MAX_PAYLOAD`).
`OTA_CHUNK` reserves the leading 4 bytes for the offset, so the data
slice per chunk is at most 1020 bytes — `tools/ota_send.py` defaults to
256 bytes per chunk because that gives a smooth progress display and
fits comfortably under the chunk size limit.

`SEQ` is owned by the framing reliable layer:
- Host increments SEQ for each new send and waits for an ACK with
  matching SEQ before sending the next frame.
- Bootloader idempotently re-ACKs a duplicate SEQ without re-running
  the work — this is what keeps the protocol robust against a missed
  ACK on the wire.
- Bad CRC at the receiver causes the frame to be silently dropped; the
  ARQ retries on host timeout.

## State machine (bootloader side)

```
                +------------+      OTA_BEGIN
   reset --->   | AWAIT_BEGIN| ----------------> erase slot, ACK
                +------------+
                      |  any other type → NACK
                      v
                +------------+      OTA_CHUNK
                | RECEIVING  | ----------------> flash_write_bytes(addr, data, N)
                +------------+                    ACK on success, NACK on flash err
                      |  OTA_END
                      v
                +------------+      verify_slot()
                | DONE       | ----------------> swap active flag, STATUS=ok
                +------------+                    NVIC_SystemReset()
                      |  verify failed
                      v
                +------------+      STATUS=verify_failed
                | HALT       | ----------------> bootloader_halt() (slow blink)
                +------------+
```

`AWAIT_BEGIN` also handles `PING` (replies `PONG`) — the host uses this
to confirm the chip is in OTA mode before starting the transfer.

## Active-slot swap (mid-OTA power-cut safety)

After a successful verify, the bootloader commits two metadata blobs
in this order:

1. **Target slot** — `active=1`, `monotonic_counter = max(both slots) + 1`.
2. **Previously-active slot** — `active=0`, `monotonic_counter` preserved.

The slot-pick decision tree in `main.c` resolves the awkward case:

| State | What happens on next boot |
|---|---|
| Power-cut **before step 1** lands | Both slots untouched. Previously-active slot still wins. Image dropped — no harm done. |
| Power-cut **between steps 1 and 2** | Both slots have `active=1`. Decision tree picks the higher `monotonic_counter` — by construction the freshly-OTA'd slot. Step 2 is just tidying. |
| Power-cut **after step 2** | Clean A/B swap committed. |

The `flash_slot_commit_metadata` primitive is itself power-cut safe
(erase, then write, then read-back-verify). A power cut inside the
metadata sector erase leaves it all-`0xFF`, which the parser rejects
(CRC fails) and the slot-pick treats as "invalid" — falls back to the
other slot.

## Operator forced recovery

If both slots ever get bricked at the application layer, force OTA
mode by writing the magic via OpenOCD before triggering a reset:

```
openocd -f board/st_nucleo_f4.cfg \
    -c "init" -c "reset halt" \
    -c "mww 0x40002850 0x4F544131" \
    -c "reset run" -c "exit"
```

`0x40002850` is the address of `RTC->BKP0R` on STM32F411.

Then run `tools/ota_send.py` against either slot. The slot-A bootloader
will accept the new image because both slots' metadata is invalid (the
"refuse to overwrite the active slot" guard treats no-active-slot as
"slot A is the implicit active", so target slot B will work; flashing
slot A is a separate `make flash` operation if needed).

## Production gap notes

The protocol provides authenticity through the per-image ECDSA-P256
signature only.

- **No transport authentication.** Anyone with physical access to the
  UART pins can stream frames; the bootloader will accept and verify
  them, but a tampered image fails the existing signature check, so the
  worst an on-wire attacker can do is DoS the OTA path.
- **No transport replay protection.** Replaying an old signed image is
  equivalent to flashing it via OpenOCD; the cure for downgrade attacks
  is anti-rollback, which lands in Phase 1.9 as a `monotonic_counter`
  floor check before verify.
- **No mid-stream encryption.** Plan 001 §"Out of scope" excludes
  encrypted firmware images by design.

## Backup-register reservation

| Register | Owner | Notes |
|---|---|---|
| `BKP0R`  | Phase 1.8 OTA magic | Cleared by the bootloader on every entry. |
| `BKP1R..BKP19R` | reserved | Future use (e.g. anti-rollback floor cache, last-failure code). |

Other code that writes `BKP0R` would falsely trigger OTA mode. The
`drivers/inc/rtc_backup.h` header is the single point of contact for
DR0; new consumers should pick a different DR slot.

## Log-line grammar

| Line | Meaning |
|---|---|
| `BL: stm32-bare-metal bootloader (Phase 1.8)` | Bootloader started |
| `BL: OTA mode entered` | DR0 magic was set; entering OTA receiver |
| `OTA: ready` | Receiver is listening on USART2 |
| `OTA: BEGIN slot=<X> size=<N>` | OTA_BEGIN accepted, slot erased |
| `OTA: refusing to write active slot` | OTA_BEGIN rejected (target == previously-active slot) |
| `OTA: END but size mismatch` | OTA_END seen but received_bytes != expected_size |
| `OTA: END verifying...` | About to call verify_slot() |
| `OTA: verify FAILED` | verify_slot() rejected the freshly-flashed image |
| `OTA: metadata commit failed` | flash_slot_commit_metadata() returned an error |
| `OTA: ok slot=<X>` | Active flag flipped; chip about to NVIC_SystemReset |
| `OTA: decoder init failed` | Internal error setting up the framing decoder |

## See also

- [`lib/framing/inc/framing.h`](../../../../lib/framing/inc/framing.h) — wire format spec.
- [`apps/bootloader/loader/ota.c`](../../../../apps/bootloader/loader/ota.c) — receiver state machine.
- [`tools/ota_send.py`](../../../../tools/ota_send.py) — host driver.
- [`scripts/run_ota_test.py`](../../../../scripts/run_ota_test.py) — HIL flow.
