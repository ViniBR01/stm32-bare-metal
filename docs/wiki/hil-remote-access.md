# HIL Remote Access and MCP Tools

This page covers how to reach the Pi over SSH and expose HIL tests as Claude Code tools.

**Current setup: local network only.** The Pi (`pi-hil`) is reachable via mDNS on the local
network. The SSH config uses `BindAddress` to bypass corporate VPN routing (see below).

---

## Part 1 — SSH Setup (local network)

### Pi details

- Hostname: `pi-hil.local` (mDNS), IP: `10.0.0.245`
- Username: `pi`
- Boards: 2x NUCLEO-F411RE (see "Multi-Board Setup" section below)

### SSH key

Generate a dedicated key and install it on the Pi:

```sh
ssh-keygen -t ed25519 -f ~/.ssh/hil_pi -N ""
ssh-copy-id -i ~/.ssh/hil_pi.pub pi@pi-hil.local
```

### SSH config entry

Add to `~/.ssh/config`. The `BindAddress` is required on this machine because the corporate
VPN routes `10.0.0.x` traffic through `utun4` instead of `en0` — binding the source IP forces
the connection through the local network interface.

```
Host hil-pi
    HostName 10.0.0.245
    User pi
    IdentityFile ~/.ssh/hil_pi
    BindAddress 10.0.0.218
    BatchMode yes
    ConnectTimeout 10
```

> **Note:** `BindAddress 10.0.0.218` is the dev machine's local IP. If it changes (DHCP),
> update this line. To make it stable, reserve the MAC address in your router's DHCP config.

### Verify

```sh
ssh hil-pi "uname -a && ls /dev/ttyACM*"
```

Should print the Pi's kernel string and `/dev/ttyACM0`.

### Set the env var

```sh
echo 'export HIL_PI_SSH=hil-pi' >> ~/.zshrc
export HIL_PI_SSH=hil-pi
```

### Future: remote access

If remote access is ever needed from the work laptop, the recommended option for
corporate environments is **Cloudflare Tunnel** (`cloudflared`) — outbound HTTPS from the Pi,
no VPN or port-forwarding required. See Cloudflare Zero Trust docs for setup.

### Set the env var

The MCP server and any scripts read `HIL_PI_SSH`:

```sh
export HIL_PI_SSH=<user>@<pi-tailscale-hostname>
```

Add to `~/.zshrc` (or `~/.bashrc`) for persistence.

### Verify

```sh
ssh $HIL_PI_SSH "uname -a && ls /dev/ttyACM*"
```

Should print the Pi's kernel string and the NUCLEO board's USB serial device.

---

## Part 2 — MCP HIL Server

### What it does

`scripts/mcp_hil_server.py` is a Python stdio MCP server that Claude Code loads automatically via `.mcp.json`. It exposes two tools:

| Tool | What it does |
|---|---|
| `hil_status()` | SSH to Pi, check reachability and `/dev/ttyACM*` presence |
| `hil_run_tests(skip_build, skip_flash)` | rsync → build → flash → run tests → return structured results |

The server runs locally on your dev machine. It SSH's to the Pi on demand — no daemon is needed on the Pi.

**Code sync**: before every build, the server rsyncs the entire working tree to `~/stm32-bare-metal/` on the Pi. This means uncommitted local changes are included in the test run immediately.

### Install the MCP dependency

```sh
pip install mcp
```

This is a dev-only tool; it does not need to be added to any project requirements file.

### Configuration

Claude Code picks up `.mcp.json` at the project root automatically. The `HIL_PI_SSH` env var must be set in the shell that launches Claude Code (set it in `~/.zshrc`).

No hostname or credentials are hard-coded in any repo file.

### Verifying the MCP server

Check imports:

```sh
cd <project-root>
python3 -c "import scripts.mcp_hil_server; print('OK')"
```

Inside a Claude Code session, ask Claude to call `hil_status`. It should return:

```json
{
  "pi_reachable": true,
  "board_connected": true,
  "error": null
}
```

### Running tests through Claude

Once the MCP server is active, Claude can drive the full HIL cycle autonomously:

```
"Run HIL tests on the current code"
  → Claude calls hil_run_tests()
  → rsync, build, flash, serial capture, parse
  → returns {passed, summary, tests, regressions}
  → Claude reads results and iterates on the code
```

For faster iteration when firmware is already flashed:

```
"Re-run the tests without reflashing"
  → Claude calls hil_run_tests(skip_build=true, skip_flash=true)
```

### Tool return format

`hil_run_tests` returns:

```json
{
  "passed": true,
  "summary": { "total": 60, "passed": 60, "failed": 0 },
  "tests": [
    { "name": "spi1_dma_psc2_256B", "status": "PASS", "cycles": 4811,
      "metrics": { "throughput_kbps": 5333 } }
  ],
  "regressions": [],
  "error": null
}
```

On infrastructure failure (build error, serial timeout, board not connected):

```json
{
  "passed": false,
  "error": "HIL infrastructure error (exit 2). stdout: ..."
}
```

### Rsync excludes

The rsync step skips `build/`, `.git/`, `__pycache__/`, and `*.pyc` to keep transfers fast. Everything else (source files, scripts, baselines, Makefiles) is synced.

---

## Multi-Board Setup

Two NUCLEO-F411RE boards are connected to the Pi, each with a dedicated role:

| Role | ST-LINK Serial | Stable Path | Purpose |
|------|---------------|-------------|---------|
| `ci` | `066BFF554869774867234426` | `/dev/serial/by-id/usb-STMicroelectronics_STM32_STLink_066BFF554869774867234426-if02` | Automated CI tests (GitHub Actions) |
| `dev` | `066CFF3833554B3043154235` | `/dev/serial/by-id/usb-STMicroelectronics_STM32_STLink_066CFF3833554B3043154235-if02` | Manual/agent testing (MCP, SSH) |

### How board selection works

The `--board` flag in `run_hil_tests.py` resolves to the correct serial port and ST-LINK serial:

```sh
# CI (automated — used by GitHub Actions)
python3 scripts/run_hil_tests.py --board ci --timeout 240

# Dev (manual — used by MCP server and SSH sessions)
python3 scripts/run_hil_tests.py --board dev --timeout 180
```

OpenOCD is told which probe to use via `adapter serial <serial>`, so both boards can be
connected simultaneously without conflict.

### Why no udev rules

Linux auto-creates stable symlinks in `/dev/serial/by-id/` based on USB serial numbers.
These survive reboots and do not depend on USB enumeration order (unlike `/dev/ttyACM0`).
No custom udev rules are needed.

### Replacing a board

If a board is physically replaced, update its serial number in the `BOARD_REGISTRY` dict
at the top of `scripts/run_hil_tests.py`. The new serial can be found with:

```sh
ls /dev/serial/by-id/usb-STMicroelectronics_STM32_STLink_*
```

### Verify both boards

```sh
ssh hil-pi "ls /dev/serial/by-id/usb-STMicroelectronics_STM32_STLink_*"
```

Should list two paths — one for each board.

---

## Bootloader prerequisite (Plan 001 Phase 1.5+)

Sector 0 of every board now holds the bootloader from `apps/bootloader/loader/`,
and slot A (0x08010000) holds the cli_simple HIL image.  HIL CI re-flashes
slot A on every run; sector 0 is **never** touched by the runner, so each
board needs the bootloader programmed once before it can host HIL runs.

```sh
ssh hil-pi
cd ~/stm32-bare-metal
make EXAMPLE=bootloader
# CI board:
python3 scripts/run_hil_tests.py --hla-serial 066BFF554869774867234426 --skip-build --skip-flash  # confirms board is reachable
make flash-bootloader EXAMPLE=bootloader     # for the currently-attached probe;
                                              # repeat after switching the cable
                                              # to the other board.
```

A bricked board (bad bootloader) is recovered via mass erase — see
[plans/001-bootloader/bootloader-skeleton.md](plans/001-bootloader/bootloader-skeleton.md#recovery--bricked-board).

The Pi runner also needs the Python `cryptography` package once
(`pip3 install cryptography`); the CI workflow's "Verify Python signing
dependencies" step will fail loudly if this drifts.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `HIL_PI_SSH not set` | `export HIL_PI_SSH=<user>@<pi-hostname>` in `~/.zshrc` |
| SSH asks for password | Run `ssh-copy-id` to install your key on the Pi |
| `board_connected: false` | Check USB cable; run `lsusb` on Pi to confirm board is enumerated |
| rsync slow | Normal on first sync; subsequent runs only transfer diffs |
| Build fails on Pi | SSH in and run `make EXAMPLE=cli_simple HIL_TEST=1` manually to see error |
| `pip install mcp` fails | Try `pip3 install mcp` or `python3 -m pip install mcp` |
| HIL run prints `BL: slot A header parse failed` | Slot A holds an unsigned or stale image; re-run `python3 scripts/run_hil_tests.py` to re-sign and re-flash. |
| Board boots but UART silent | Bootloader missing from sector 0; flash via `make flash-bootloader EXAMPLE=bootloader`. |
