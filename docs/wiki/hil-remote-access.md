# HIL Remote Access and MCP Tools

This page covers two related setups that together let you (and Claude) run real hardware tests from anywhere:

1. **Tailscale** — reach the Pi over SSH from any network
2. **MCP HIL server** — expose `hil_run_tests` and `hil_status` as Claude Code tools

---

## Part 1 — Tailscale Remote SSH

### Why Tailscale

The Pi is normally only reachable on the local network. Tailscale creates a mesh VPN: both machines join the same Tailscale network and each gets a stable MagicDNS hostname. No router port-forwarding needed, works from any network in the world.

### Install on the Pi

```sh
curl -fsSL https://tailscale.com/install.sh | sh
sudo tailscale up
```

Follow the auth URL printed to the terminal. Once authenticated, the Pi will appear in your Tailscale admin panel.

### Install on macOS (dev machine)

```sh
brew install tailscale
sudo tailscale up
```

Or download the macOS app from tailscale.com. Sign in to the **same Tailscale account**.

### Find the Pi's hostname

```sh
tailscale status
```

The Pi will have a MagicDNS name like `raspberrypi.tail-abc123.ts.net` or just `raspberrypi` if MagicDNS is enabled. Either form works with SSH.

### Set up SSH key auth (required)

Automation tools (rsync, the MCP server) need passwordless SSH. Set up a dedicated key:

```sh
ssh-keygen -t ed25519 -f ~/.ssh/hil_pi -N ""
ssh-copy-id -i ~/.ssh/hil_pi.pub <user>@<pi-tailscale-hostname>
```

Optional: add to `~/.ssh/config` to avoid specifying the key each time:

```
Host hil-pi
    HostName <pi-tailscale-hostname>
    User <user>
    IdentityFile ~/.ssh/hil_pi
    BatchMode yes
```

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

## Troubleshooting

| Symptom | Fix |
|---|---|
| `HIL_PI_SSH not set` | `export HIL_PI_SSH=<user>@<pi-hostname>` in `~/.zshrc` |
| SSH asks for password | Run `ssh-copy-id` to install your key on the Pi |
| `board_connected: false` | Check USB cable; run `lsusb` on Pi to confirm board is enumerated |
| rsync slow | Normal on first sync; subsequent runs only transfer diffs |
| Build fails on Pi | SSH in and run `make EXAMPLE=cli_simple HIL_TEST=1` manually to see error |
| `pip install mcp` fails | Try `pip3 install mcp` or `python3 -m pip install mcp` |
