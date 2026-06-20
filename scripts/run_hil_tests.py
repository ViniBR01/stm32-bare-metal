#!/usr/bin/env python3
"""
Hardware-in-the-loop test runner for STM32 bare-metal project.

This script automates the full HIL test workflow:
1. Build firmware with HIL_TEST=1
2. Flash to STM32 board via OpenOCD
3. Connect to serial port and send 'run_all_tests' command
4. Parse test output and extract results + performance metrics
5. Validate metrics against baseline thresholds
6. Exit with status code (0=pass, 1=fail, 2=error)

Usage:
    python3 scripts/run_hil_tests.py [--skip-build] [--skip-flash]

Options:
    --skip-build      Skip firmware build step (use existing binary)
    --skip-flash      Skip flashing step (assumes firmware already loaded)
    --baseline PATH   Custom path to baseline JSON (default: tests/baselines/performance.json)
    --timeout SEC     Serial timeout in seconds (default: 30)
    --hla-serial SN   ST-LINK serial number to pin OpenOCD and serial port.
                      Pass "" to disable pinning (default: 066BFF554869774867234426)
"""

import argparse
import glob
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path
from typing import Dict, List, Optional, Tuple
from xml.etree.ElementTree import Element, SubElement, ElementTree, indent

# Board registry: maps role names to ST-LINK serial numbers.
# The serial number is used both for OpenOCD probe selection (hla_serial) and
# to derive the stable /dev/serial/by-id/ symlink for the serial port.
BOARD_REGISTRY = {
    "ci": "066BFF554869774867234426",
    "dev": "066CFF3833554B3043154235",
}

# Default ST-LINK serial (used when --board is not specified).
DEFAULT_HLA_SERIAL = BOARD_REGISTRY["ci"]

# Color codes for terminal output
class Colors:
    GREEN = '\033[92m'
    RED = '\033[91m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    RESET = '\033[0m'
    BOLD = '\033[1m'

def log(msg: str, color: str = ''):
    """Print timestamped log message"""
    timestamp = time.strftime('%H:%M:%S')
    print(f"{color}[{timestamp}] {msg}{Colors.RESET}")

def log_success(msg: str):
    log(f"✓ {msg}", Colors.GREEN)

def log_error(msg: str):
    log(f"✗ {msg}", Colors.RED)

def log_warning(msg: str):
    log(f"⚠ {msg}", Colors.YELLOW)

def log_info(msg: str):
    log(f"ℹ {msg}", Colors.BLUE)

def get_project_root() -> Path:
    """Find project root (directory containing Makefile)"""
    script_dir = Path(__file__).parent.absolute()
    project_root = script_dir.parent
    
    if not (project_root / 'Makefile').exists():
        raise RuntimeError(f"Cannot find project root (no Makefile in {project_root})")
    
    return project_root

def build_firmware(project_root: Path) -> Path:
    """
    Build cli_simple firmware with HIL_TEST=1.

    Since Plan 001 Phase 1.5 the cli_simple image is linked at slot A
    (0x08010000) and signed by tools/sign_image.py.  This function returns
    the path to the .signed.bin — the artifact the bootloader can parse and
    jump into.  The bootloader itself is flashed manually once per board
    via `make flash-bootloader` and is never touched by CI.

    Returns:
        Path to the cli_simple_a.signed.bin file, or None on error.
    """
    log_info("Building firmware with HIL_TEST=1...")

    try:
        subprocess.run(
            ['make', 'clean'],
            cwd=project_root,
            capture_output=True,
            text=True,
            check=True
        )

        result = subprocess.run(
            ['make', 'EXAMPLE=cli_simple', 'HIL_TEST=1'],
            cwd=project_root,
            capture_output=True,
            text=True,
            check=True,
            timeout=120
        )

        elf_path = project_root / 'build' / 'apps' / 'cli' / 'cli_simple_a' / 'cli_simple_a.elf'
        signed_path = project_root / 'build' / 'apps' / 'cli' / 'cli_simple_a' / 'cli_simple_a.signed.bin'

        if not signed_path.exists():
            log_error("Build completed but signed binary not found")
            print(result.stdout)
            if result.stderr:
                log_error("Build stderr:")
                print(result.stderr)
            return None

        log_success(f"Build complete: {signed_path.relative_to(project_root)}")

        if elf_path.exists():
            size_result = subprocess.run(
                ['arm-none-eabi-size', str(elf_path)],
                capture_output=True,
                text=True
            )
            print(size_result.stdout)

        return signed_path

    except subprocess.CalledProcessError as e:
        log_error(f"Build failed with exit code {e.returncode}")
        print(e.stdout)
        print(e.stderr)
        return None
    except subprocess.TimeoutExpired:
        log_error("Build timed out after 120 seconds")
        return None

def openocd_run(hla_serial: str, *commands: str, timeout: int = 30,
                retries: int = 3) -> None:
    """Run OpenOCD commands with automatic retry on transient USB failures.

    The ST-LINK USB connection on the Pi occasionally drops (bulk transfer
    timeout, probe NACK).  These are transient — a 1-second pause and retry
    almost always succeeds.  This function retries up to `retries` times
    before raising.
    """
    cmd = ["openocd"]
    if hla_serial:
        cmd += ["-c", f"hla_serial {hla_serial}"]
    cmd += ["-f", "board/st_nucleo_f4.cfg",
            "-c", "init", "-c", "reset halt"]
    for c in commands:
        cmd += ["-c", c]
    cmd += ["-c", "exit"]

    last_err = None
    for attempt in range(retries):
        try:
            subprocess.run(cmd, check=True, capture_output=True,
                           timeout=timeout)
            return
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as e:
            last_err = e
            if attempt < retries - 1:
                import time
                time.sleep(1.0)
    raise RuntimeError(
        f"OpenOCD failed after {retries} attempts: {last_err}")


def flash_firmware(image_path: Path, hla_serial: str = '',
                   slot_base: int = 0x08010000) -> bool:
    """
    Flash a signed firmware image into slot A via OpenOCD.

    Sector 0 is reserved for the bootloader (see Plan 001 Phase 1.5) and
    must NEVER be touched by this script — it is programmed manually once
    per board via ``make flash-bootloader``.  CI only reflashes the slot.

    Args:
        image_path:  Path to the .signed.bin produced by tools/sign_image.py.
        hla_serial:  ST-LINK serial number to target.  When non-empty,
                     ``hla_serial`` is passed to OpenOCD so that it selects
                     the correct probe even when multiple ST-LINKs are
                     connected.
        slot_base:   Flash address for the .signed.bin.  Default 0x08010000
                     (slot A); Phase 1.7 will introduce slot B at a higher
                     address.

    Returns:
        True if successful, False otherwise.
    """
    log_info(f"Flashing {image_path.name} at {slot_base:#010x} via OpenOCD...")

    if hla_serial:
        log_info(f"Targeting ST-LINK serial: {hla_serial}")

    cmd = ['openocd']
    if hla_serial:
        cmd += ['-c', f'hla_serial {hla_serial}']
    cmd += [
        '-f', 'board/st_nucleo_f4.cfg',
        '-c', f'program {image_path} {slot_base:#010x} verify reset exit',
    ]

    last_err = None
    for attempt in range(3):
        try:
            subprocess.run(cmd, cwd=get_project_root(),
                           capture_output=True, text=True,
                           check=True, timeout=30)
            log_success("Flash complete")
            return True
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as e:
            last_err = e
            if attempt < 2:
                import time
                log_warning(f"Flash attempt {attempt+1} failed, retrying...")
                time.sleep(1.0)

    if isinstance(last_err, subprocess.CalledProcessError):
        log_error(f"Flash failed with exit code {last_err.returncode}")
        print(last_err.stderr or "")
    else:
        log_error("Flash timed out after 30 seconds")
    return False

def find_serial_port(hla_serial: str = '') -> Optional[str]:
    """
    Auto-detect NUCLEO serial port.

    When *hla_serial* is provided (non-empty), the function first checks for
    the stable ``/dev/serial/by-id/`` symlink that encodes the ST-LINK serial
    number.  This guarantees we talk to the same board that OpenOCD flashed,
    even when multiple ST-LINKs are connected.

    Falls back to glob-based detection for development environments that lack
    the pinned symlink (e.g. macOS or single-board setups).

    Returns:
        Serial port path or None if not found
    """
    # --- Pinned symlink (Linux, matches specific ST-LINK) ----------------
    if hla_serial:
        pinned = (f'/dev/serial/by-id/'
                  f'usb-STMicroelectronics_STM32_STLink_{hla_serial}-if02')
        if os.path.exists(pinned):
            resolved = os.path.realpath(pinned)
            log_info(f"Using pinned serial port: {pinned} -> {resolved}")
            return pinned

    # --- Glob fallback ----------------------------------------------------
    # Mac patterns
    mac_patterns = ['/dev/cu.usbmodem*']
    # Linux patterns
    linux_patterns = ['/dev/ttyACM*']

    for pattern in mac_patterns + linux_patterns:
        ports = glob.glob(pattern)
        if ports:
            # Multi-device warning: ambiguity risk when no pinned symlink
            if len(ports) > 1:
                log_warning(
                    f"Multiple serial devices found: {ports}. "
                    f"Using {ports[0]}, but this may be the WRONG board. "
                    f"Consider using --hla-serial to pin to a specific "
                    f"ST-LINK serial number."
                )
            port = ports[0]
            log_info(f"Found serial port: {port}")
            return port

    return None

def run_test_suite(port: str, baudrate: int = 115200, timeout: int = 30) -> Tuple[bool, List[str]]:
    """
    Connect to serial port, run tests, and capture output
    
    Args:
        port: Serial port path
        baudrate: Baud rate (default 115200)
        timeout: Timeout in seconds
        
    Returns:
        Tuple of (success, output_lines)
    """
    log_info(f"Connecting to {port} at {baudrate} baud...")
    
    try:
        import serial
    except ImportError:
        log_error("pyserial not installed. Run: pip3 install pyserial")
        return False, []
    
    try:
        ser = serial.Serial(port, baudrate, timeout=2)
        time.sleep(0.5)  # Let board stabilize after connection
        
        # Flush any existing data
        ser.reset_input_buffer()
        
        log_info("Sending 'run_all_tests' command...")
        ser.write(b'run_all_tests\n')
        ser.flush()
        
        # Collect output lines
        output_lines = []
        start_time = time.time()
        found_start = False
        found_end = False
        
        while time.time() - start_time < timeout:
            if ser.in_waiting:
                try:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        output_lines.append(line)
                        print(f"  {line}")
                        
                        if 'START_TESTS' in line:
                            found_start = True
                            log_info("Test sequence started")
                        
                        if 'END_TESTS' in line:
                            found_end = True
                            log_success("Test sequence complete")
                            break
                except UnicodeDecodeError:
                    continue
            else:
                time.sleep(0.1)
        
        ser.close()
        
        if not found_start:
            log_warning("Did not find START_TESTS marker")
        if not found_end and found_start:
            log_warning("Found START_TESTS but not END_TESTS (timeout?)")
        
        return found_start and found_end, output_lines
        
    except serial.SerialException as e:
        log_error(f"Serial communication error: {e}")
        return False, []
    except Exception as e:
        log_error(f"Unexpected error: {e}")
        return False, []

def parse_test_output(lines: List[str]) -> Dict:
    """
    Parse test output and extract results + metrics

    Parses lines in format (single-sample, legacy):
        TEST:<name>:<PASS|FAIL>:cycles=<value>:<metric>=<value>

    And the extended repeated-sampling format:
        TEST:<name>:<PASS|FAIL>:cycles=<value>:<metric>=<value>:samples=<N>:integrity_passes=<M>

    And Unity format:
        filename.c:line:test_name:PASS
        filename.c:line:test_name:FAIL:message

    Returns:
        Dict with structure:
        {
            'tests': [{'name': ..., 'status': ..., 'cycles': ..., 'metrics': {...},
                       'samples': N, 'integrity_passes': M}, ...],
            'summary': {'total': N, 'passed': N, 'failed': N}
        }
    """
    results = {
        'tests': [],
        'summary': {'total': 0, 'passed': 0, 'failed': 0}
    }

    for line in lines:
        # Parse TEST: format (performance tests — single-sample or sampled)
        # Extended format:  TEST:<name>:<PASS|FAIL>:cycles=<v>:<metric>=<v>:samples=<N>:integrity_passes=<M>
        # Legacy format:    TEST:<name>:<PASS|FAIL>:cycles=<v>:<metric>=<v>
        match = re.match(r'^TEST:([^:]+):(PASS|FAIL):cycles=(\d+):([^=:]+)=(\d+)(.*)', line)
        if match:
            name, status, cycles, metric_name, metric_value, extra = match.groups()
            entry = {
                'name': name,
                'status': status,
                'cycles': int(cycles),
                'metrics': {metric_name: int(metric_value)}
            }
            # Parse optional extended fields: :samples=N:integrity_passes=M
            samples_match = re.search(r':samples=(\d+)', extra)
            integrity_match = re.search(r':integrity_passes=(\d+)', extra)
            if samples_match:
                entry['samples'] = int(samples_match.group(1))
            if integrity_match:
                entry['integrity_passes'] = int(integrity_match.group(1))
            results['tests'].append(entry)
            results['summary']['total'] += 1
            if status == 'PASS':
                results['summary']['passed'] += 1
            else:
                results['summary']['failed'] += 1
            continue

        # Parse Unity format
        match = re.match(r'^([^:]+\.c):(\d+):(\w+):(PASS|FAIL)(.*)$', line)
        if match:
            source, lineno, test_name, status, rest = match.groups()
            results['tests'].append({
                'name': test_name,
                'status': status,
                'source': source,
                'line': int(lineno),
                'message': rest.lstrip(':') if rest else ''
            })
            results['summary']['total'] += 1
            if status == 'PASS':
                results['summary']['passed'] += 1
            else:
                results['summary']['failed'] += 1

    return results

def load_baselines(baseline_path: Path) -> Dict:
    """Load performance baselines from JSON file"""
    if not baseline_path.exists():
        log_warning(f"Baseline file not found: {baseline_path}")
        return {}
    
    try:
        with open(baseline_path) as f:
            return json.load(f)
    except json.JSONDecodeError as e:
        log_error(f"Invalid baseline JSON: {e}")
        return {}

def check_baselines(results: Dict, baselines: Dict) -> Tuple[bool, List[Dict]]:
    """
    Validate performance metrics against baselines

    Returns:
        Tuple of (all_pass, regressions) where regressions is a list of dicts:
        {'name': ..., 'metric': ..., 'expected': ..., 'actual': ..., 'tolerance': ...}
    """
    if not baselines:
        log_warning("No baselines loaded - skipping validation")
        return True, []

    all_pass = True
    regressions = []

    log_info("Validating against baselines...")

    for test in results['tests']:
        test_name = test['name']

        # Report integrity pass rate for sampled tests regardless of baseline
        if 'samples' in test and 'integrity_passes' in test:
            n = test['samples']
            m = test['integrity_passes']
            if m >= n:
                log_success(f"  {test_name}: Integrity {m}/{n} (all clean)")
            elif m >= (n - 1):
                log_warning(f"  {test_name}: Integrity {m}/{n} (1 transient error — within tolerance)")
            else:
                log_error(f"  {test_name}: Integrity {m}/{n} — too many corruptions, treating as FAIL")
                all_pass = False
                regressions.append({
                    'name': test_name,
                    'metric': 'integrity',
                    'expected': n,
                    'actual': m,
                    'tolerance': 1,
                })

        if test_name not in baselines:
            log_info(f"  {test_name}: No baseline (new test)")
            continue

        baseline = baselines[test_name]

        # Check if baseline has values (not null)
        if baseline.get('cycles') is None:
            log_info(f"  {test_name}: Baseline not populated yet")
            continue

        # Get tolerance
        tolerance_pct = baseline.get('tolerance_percent', 10)

        # Check cycles (median for sampled tests)
        if 'cycles' in test:
            expected_cycles = baseline['cycles']
            actual_cycles = test['cycles']
            min_cycles = expected_cycles * (100 - tolerance_pct) / 100
            max_cycles = expected_cycles * (100 + tolerance_pct) / 100

            if actual_cycles < min_cycles or actual_cycles > max_cycles:
                log_error(f"  {test_name}: Cycle count regression")
                log_error(f"    Expected: {expected_cycles} ±{tolerance_pct}%")
                log_error(f"    Actual: {actual_cycles}")
                all_pass = False
                regressions.append({
                    'name': test_name,
                    'metric': 'cycles',
                    'expected': expected_cycles,
                    'actual': actual_cycles,
                    'tolerance': tolerance_pct,
                })
            else:
                log_success(f"  {test_name}: Cycles OK ({actual_cycles})")

        # Check other metrics
        if 'metrics' in test:
            for metric_name, metric_value in test['metrics'].items():
                if metric_name in baseline:
                    expected = baseline[metric_name]
                    if expected is not None:
                        min_val = expected * (100 - tolerance_pct) / 100
                        max_val = expected * (100 + tolerance_pct) / 100

                        if metric_value < min_val or metric_value > max_val:
                            log_error(f"  {test_name}: {metric_name} regression")
                            log_error(f"    Expected: {expected} ±{tolerance_pct}%")
                            log_error(f"    Actual: {metric_value}")
                            all_pass = False
                            regressions.append({
                                'name': test_name,
                                'metric': metric_name,
                                'expected': expected,
                                'actual': metric_value,
                                'tolerance': tolerance_pct,
                            })
                        else:
                            log_success(f"  {test_name}: {metric_name} OK ({metric_value})")

    return all_pass, regressions

def write_junit_xml(results: Dict, regressions: List[Dict], output_path: str):
    """
    Write JUnit XML report compatible with dorny/test-reporter@v3.

    Unity test lines → classname="test_harness"
    TEST: performance lines (have 'cycles' field) → classname="spi_perf"
    Baseline regressions → <failure> on the matching spi_perf testcase
    Integrity failures → <failure> on the matching spi_perf testcase
    """
    tests = results.get('tests', [])

    # Build a set of regression names for fast lookup
    regression_by_name: Dict[str, List[Dict]] = {}
    for reg in regressions:
        regression_by_name.setdefault(reg['name'], []).append(reg)

    failures = 0
    testcase_elements = []

    for test in tests:
        name = test['name']
        status = test.get('status', 'PASS')

        if 'cycles' in test:
            # Performance / sampled test → spi_perf
            tc = Element('testcase', classname='spi_perf', name=name, time='0')
            test_regressions = regression_by_name.get(name, [])
            if test_regressions:
                for reg in test_regressions:
                    if reg['metric'] == 'integrity':
                        msg = (f"Integrity: {reg['actual']}/{reg['expected']} passes")
                    else:
                        msg = (
                            f"{reg['metric'].capitalize()} regression: "
                            f"expected {reg['expected']} ±{reg['tolerance']}%, "
                            f"actual {reg['actual']}"
                        )
                    SubElement(tc, 'failure', message=msg)
                    failures += 1
        else:
            # Unity test → test_harness
            tc = Element('testcase', classname='test_harness', name=name, time='0')
            if status == 'FAIL':
                message = test.get('message', '')
                SubElement(tc, 'failure', message=message)
                failures += 1

        testcase_elements.append(tc)

    total = len(testcase_elements)

    testsuites = Element('testsuites')
    testsuite = SubElement(
        testsuites,
        'testsuite',
        name='HIL Tests',
        tests=str(total),
        failures=str(failures),
        errors='0',
        time='0',
    )
    for tc in testcase_elements:
        testsuite.append(tc)

    indent(testsuites)

    tree = ElementTree(testsuites)
    tree.write(output_path, encoding='unicode', xml_declaration=True)
    log_info(f"JUnit XML written to {output_path} ({total} tests, {failures} failures)")


def print_summary(results: Dict):
    """Print test results summary"""
    summary = results['summary']
    total = summary['total']
    passed = summary['passed']
    failed = summary['failed']
    
    print()
    print("=" * 50)
    print(f"{Colors.BOLD}Test Results Summary{Colors.RESET}")
    print("=" * 50)
    print(f"Total:  {total}")
    print(f"{Colors.GREEN}Passed: {passed}{Colors.RESET}")
    
    if failed > 0:
        print(f"{Colors.RED}Failed: {failed}{Colors.RESET}")
    else:
        print(f"Failed: {failed}")
    
    print("=" * 50)
    
    if failed == 0 and total > 0:
        log_success("All tests passed!")
    elif total == 0:
        log_warning("No tests found in output")
    else:
        log_error(f"{failed} test(s) failed")

def main():
    parser = argparse.ArgumentParser(
        description='Hardware-in-the-loop test runner for STM32',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument('--skip-build', action='store_true',
                        help='Skip firmware build step')
    parser.add_argument('--skip-flash', action='store_true',
                        help='Skip flashing step')
    parser.add_argument('--baseline', type=Path,
                        default='tests/baselines/performance.json',
                        help='Path to baseline JSON file')
    parser.add_argument('--timeout', type=int, default=30,
                        help='Serial timeout in seconds')
    parser.add_argument('--board', choices=['ci', 'dev'],
                        help='Board role: "ci" (automated CI) or "dev" (manual/agent testing)')
    parser.add_argument('--junit-xml', default='hil-test-results.xml',
                        help='Path to write JUnit XML report (default: hil-test-results.xml)')
    parser.add_argument('--hla-serial', default=DEFAULT_HLA_SERIAL,
                        help=('ST-LINK serial number to pin OpenOCD and serial '
                              'port selection. Pass empty string "" to disable '
                              'pinning. (default: %(default)s)'))

    args = parser.parse_args()

    results = {'tests': [], 'summary': {'total': 0, 'passed': 0, 'failed': 0}}
    regressions: List[Dict] = []
    exit_code = 2

    try:
        project_root = get_project_root()
        log_info(f"Project root: {project_root}")

        # Build firmware (produces cli_simple.signed.bin).
        if not args.skip_build:
            image_path = build_firmware(project_root)
            if not image_path:
                return 2
        else:
            log_warning("Skipping build (--skip-build)")
            image_path = (project_root / 'build' / 'apps' / 'cli' / 'cli_simple_a'
                          / 'cli_simple_a.signed.bin')
            if not image_path.exists():
                log_error("No existing signed image found - build required")
                return 2

        # Resolve board role to serial number (overrides --hla-serial).
        hla_serial = args.hla_serial
        if args.board:
            hla_serial = BOARD_REGISTRY[args.board]

        # Flash the slot-A image only — the bootloader in sector 0 is
        # programmed once per board and is never touched here.
        if not args.skip_flash:
            # Erase metadata so floor=0 — prevents stale counters from a
            # prior run rejecting this IMAGE_VERSION=1 image.
            log_info("Erasing metadata sectors for clean boot...")
            openocd_run(hla_serial,
                        "flash erase_sector 0 1 1",
                        "flash erase_sector 0 2 2")

            if not flash_firmware(image_path, hla_serial=hla_serial):
                return 2
        else:
            log_warning("Skipping flash (--skip-flash)")

        # Wait for board to reset
        time.sleep(2)

        # Find serial port
        port = find_serial_port(hla_serial=hla_serial)
        if not port:
            log_error("Serial port not found - is the board connected?")
            return 2

        # Run tests
        success, output_lines = run_test_suite(port, timeout=args.timeout)
        if not success:
            log_error("Failed to run tests (timeout or serial error)")
            return 2

        # Parse results
        results = parse_test_output(output_lines)

        if results['summary']['total'] == 0:
            log_error("No tests found in output")
            return 2

        # Print summary
        print_summary(results)

        # Check baselines
        baseline_path = project_root / args.baseline
        baselines = load_baselines(baseline_path)
        baseline_pass, regressions = check_baselines(results, baselines)

        # Determine exit code
        if results['summary']['failed'] > 0:
            exit_code = 1
        elif not baseline_pass:
            log_error("Baseline validation failed")
            exit_code = 1
        else:
            exit_code = 0

        return exit_code

    except KeyboardInterrupt:
        log_warning("\nInterrupted by user")
        return 2
    except Exception as e:
        log_error(f"Unexpected error: {e}")
        import traceback
        traceback.print_exc()
        return 2
    finally:
        write_junit_xml(results, regressions, args.junit_xml)

if __name__ == '__main__':
    sys.exit(main())
