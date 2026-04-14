#!/usr/bin/env python3
"""
MCP HIL server for STM32 bare-metal project.

Exposes two tools to Claude Code:
  - hil_status()                          check Pi reachability + board presence
  - hil_run_tests(skip_build, skip_flash) sync code, build, flash, run, return results

Runs on the dev machine (macOS). SSH's to the Pi to execute build/flash/test.
Rsyncs the working tree to the Pi before building so uncommitted changes are tested.

Requirements:
  pip install mcp

Configuration:
  export HIL_PI_SSH=<user>@<pi-tailscale-hostname>   # e.g. pi@raspberrypi.tail-abc123.ts.net

Usage (invoked automatically by Claude Code via .mcp.json):
  python3 scripts/mcp_hil_server.py
"""

import asyncio
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Any

import mcp.server.stdio
import mcp.types as types
from mcp.server import Server

# Import shared helpers from existing test runner — no duplication
sys.path.insert(0, str(Path(__file__).parent))
from run_hil_tests import load_baselines, parse_test_output

# ── Constants ─────────────────────────────────────────────────────────────────

REMOTE_DIR = "~/stm32-bare-metal"
BASELINE_REL = "tests/baselines/performance.json"
RSYNC_TIMEOUT = 60   # seconds
SSH_TIMEOUT = 15     # seconds for simple connectivity checks
TEST_TIMEOUT = 180   # seconds for full build + flash + test cycle

# ── Helpers ───────────────────────────────────────────────────────────────────

def get_pi_ssh() -> str:
    val = os.environ.get("HIL_PI_SSH", "").strip()
    if not val:
        raise RuntimeError(
            "HIL_PI_SSH environment variable not set.\n"
            "Example: export HIL_PI_SSH=pi@raspberrypi.tail-abc123.ts.net\n"
            "See docs/wiki/hil-remote-access.md for setup instructions."
        )
    return val


def get_project_root() -> Path:
    return Path(__file__).parent.parent.resolve()


def ssh_run(pi_ssh: str, cmd: str, timeout: int = SSH_TIMEOUT) -> subprocess.CompletedProcess:
    """Run a single command on the Pi over SSH. Returns CompletedProcess."""
    return subprocess.run(
        ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=10", pi_ssh, cmd],
        capture_output=True,
        text=True,
        timeout=timeout,
    )


def rsync_to_pi(pi_ssh: str, local_root: Path) -> tuple[bool, str]:
    """
    Rsync local project tree to Pi:~/stm32-bare-metal/.
    Excludes build artifacts and .git to keep the transfer fast.
    Returns (success, error_message).
    """
    remote_target = f"{pi_ssh}:{REMOTE_DIR}/"
    result = subprocess.run(
        [
            "rsync", "-az", "--delete",
            "--exclude=build/",
            "--exclude=.git/",
            "--exclude=__pycache__/",
            "--exclude=*.pyc",
            f"{local_root}/",
            remote_target,
        ],
        capture_output=True,
        text=True,
        timeout=RSYNC_TIMEOUT,
    )
    if result.returncode != 0:
        return False, f"rsync failed (exit {result.returncode}): {result.stderr.strip()}"
    return True, ""


def build_regressions(results: dict, baselines: dict) -> list[dict]:
    """
    Compare parsed test results against baselines.
    Returns a list of regression dicts instead of printing to stdout.
    """
    regressions = []
    for test in results.get("tests", []):
        name = test["name"]
        if name not in baselines:
            continue
        baseline = baselines[name]
        if baseline.get("cycles") is None:
            continue
        tolerance_pct = baseline.get("tolerance_percent", 10)

        # Check cycle count
        if "cycles" in test:
            expected = baseline["cycles"]
            actual = test["cycles"]
            lo = expected * (100 - tolerance_pct) / 100
            hi = expected * (100 + tolerance_pct) / 100
            if not (lo <= actual <= hi):
                regressions.append({
                    "test_name": name,
                    "metric": "cycles",
                    "expected": expected,
                    "actual": actual,
                    "tolerance_pct": tolerance_pct,
                })

        # Check additional metrics (e.g. throughput_kbps)
        for metric_name, metric_value in test.get("metrics", {}).items():
            if metric_name not in baseline or baseline[metric_name] is None:
                continue
            expected = baseline[metric_name]
            lo = expected * (100 - tolerance_pct) / 100
            hi = expected * (100 + tolerance_pct) / 100
            if not (lo <= metric_value <= hi):
                regressions.append({
                    "test_name": name,
                    "metric": metric_name,
                    "expected": expected,
                    "actual": metric_value,
                    "tolerance_pct": tolerance_pct,
                })
    return regressions


# ── MCP Server ────────────────────────────────────────────────────────────────

app = Server("hil-server")


@app.list_tools()
async def list_tools() -> list[types.Tool]:
    return [
        types.Tool(
            name="hil_status",
            description=(
                "Check whether the Raspberry Pi HIL runner is reachable over SSH "
                "and the STM32 NUCLEO board is connected (looks for /dev/ttyACM* on Pi). "
                "Returns JSON: {pi_reachable: bool, board_connected: bool, error: str|null}. "
                "Call this first to verify the setup before running tests."
            ),
            inputSchema={
                "type": "object",
                "properties": {},
                "required": [],
            },
        ),
        types.Tool(
            name="hil_run_tests",
            description=(
                "Sync the current working tree to the Pi, then build + flash + run HIL tests "
                "on the connected STM32 NUCLEO-F411RE board. Uncommitted local changes are "
                "included via rsync. Returns structured JSON results with pass/fail, metrics, "
                "and any performance regressions. "
                "Use skip_build=true when only re-running tests without firmware changes. "
                "Use skip_flash=true when firmware is already loaded from a previous run."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "skip_build": {
                        "type": "boolean",
                        "description": "Skip make clean + make. Reuse existing binary on Pi.",
                        "default": False,
                    },
                    "skip_flash": {
                        "type": "boolean",
                        "description": "Skip OpenOCD flash step. Assumes firmware is already loaded.",
                        "default": False,
                    },
                },
                "required": [],
            },
        ),
    ]


@app.call_tool()
async def call_tool(name: str, arguments: dict[str, Any]) -> list[types.TextContent]:
    try:
        if name == "hil_status":
            result = await _hil_status()
        elif name == "hil_run_tests":
            result = await _hil_run_tests(
                skip_build=arguments.get("skip_build", False),
                skip_flash=arguments.get("skip_flash", False),
            )
        else:
            result = {"error": f"Unknown tool: {name}"}
    except Exception as exc:
        result = {"error": str(exc)}

    return [types.TextContent(type="text", text=json.dumps(result, indent=2))]


async def _hil_status() -> dict:
    try:
        pi_ssh = get_pi_ssh()
    except RuntimeError as exc:
        return {"pi_reachable": False, "board_connected": False, "error": str(exc)}

    # Check SSH reachability
    try:
        proc = ssh_run(pi_ssh, "true", timeout=15)
        pi_reachable = proc.returncode == 0
    except subprocess.TimeoutExpired:
        return {"pi_reachable": False, "board_connected": False, "error": "SSH timed out"}
    except Exception as exc:
        return {"pi_reachable": False, "board_connected": False, "error": str(exc)}

    if not pi_reachable:
        return {
            "pi_reachable": False,
            "board_connected": False,
            "error": f"SSH failed: {proc.stderr.strip()}",
        }

    # Check for connected NUCLEO board
    try:
        proc = ssh_run(
            pi_ssh,
            "ls /dev/ttyACM* 2>/dev/null && echo BOARD_OK || echo BOARD_NONE",
            timeout=10,
        )
        board_connected = "BOARD_OK" in proc.stdout
    except Exception:
        board_connected = False

    return {"pi_reachable": True, "board_connected": board_connected, "error": None}


async def _hil_run_tests(skip_build: bool = False, skip_flash: bool = False) -> dict:
    _empty = {"passed": False, "summary": {}, "tests": [], "regressions": []}

    try:
        pi_ssh = get_pi_ssh()
    except RuntimeError as exc:
        return {**_empty, "error": str(exc)}

    local_root = get_project_root()

    # 1. Rsync working tree to Pi (includes uncommitted changes)
    ok, err = rsync_to_pi(pi_ssh, local_root)
    if not ok:
        return {**_empty, "error": err}

    # 2. Build remote command
    flags = []
    if skip_build:
        flags.append("--skip-build")
    if skip_flash:
        flags.append("--skip-flash")
    flags_str = " ".join(flags)

    remote_cmd = (
        f"cd {REMOTE_DIR} && "
        f"python3 scripts/run_hil_tests.py --timeout 120 {flags_str}"
    )

    # 3. Run on Pi — capture stdout for parsing
    try:
        proc = ssh_run(pi_ssh, remote_cmd, timeout=TEST_TIMEOUT)
    except subprocess.TimeoutExpired:
        return {**_empty, "error": f"Remote command timed out after {TEST_TIMEOUT}s"}

    # Exit code 2 = infrastructure failure (build error, no serial port, etc.)
    if proc.returncode == 2:
        return {
            **_empty,
            "error": (
                f"HIL infrastructure error (exit 2).\n"
                f"stdout: {proc.stdout.strip()}\n"
                f"stderr: {proc.stderr.strip()}"
            ),
        }

    # Exit codes 0 (all pass) and 1 (test failures) both produce parseable output.

    # 4. Parse results from captured stdout
    output_lines = proc.stdout.splitlines()
    results = parse_test_output(output_lines)

    if results["summary"]["total"] == 0:
        return {
            **_empty,
            "error": (
                "No test results found in output — board may not have responded.\n"
                f"stdout: {proc.stdout.strip()}"
            ),
        }

    # 5. Load baselines and check regressions
    baselines = load_baselines(local_root / BASELINE_REL)
    regressions = build_regressions(results, baselines)

    passed = (
        proc.returncode == 0
        and results["summary"]["failed"] == 0
        and len(regressions) == 0
    )

    return {
        "passed": passed,
        "summary": results["summary"],
        "tests": results["tests"],
        "regressions": regressions,
        "error": None,
    }


# ── Entry point ───────────────────────────────────────────────────────────────

async def main():
    async with mcp.server.stdio.stdio_server() as (read_stream, write_stream):
        await app.run(read_stream, write_stream, app.create_initialization_options())


if __name__ == "__main__":
    asyncio.run(main())
