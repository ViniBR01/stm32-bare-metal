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
    --skip-build    Skip firmware build step (use existing binary)
    --skip-flash    Skip flashing step (assumes firmware already loaded)
    --baseline PATH Custom path to baseline JSON (default: tests/baselines/performance.json)
    --timeout SEC   Serial timeout in seconds (default: 30)
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
    Build cli_simple firmware with HIL_TEST=1
    
    Returns:
        Path to the built .elf file
    """
    log_info("Building firmware with HIL_TEST=1...")
    
    try:
        result = subprocess.run(
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
        
        # Find the built ELF file
        elf_path = project_root / 'build' / 'examples' / 'cli' / 'cli_simple' / 'cli_simple.elf'
        
        if not elf_path.exists():
            log_error("Build completed but ELF file not found")
            print(result.stdout)
            if result.stderr:
                log_error("Build stderr:")
                print(result.stderr)
            return None
        
        log_success(f"Build complete: {elf_path.relative_to(project_root)}")
        
        # Print binary size
        size_result = subprocess.run(
            ['arm-none-eabi-size', str(elf_path)],
            capture_output=True,
            text=True
        )
        print(size_result.stdout)
        
        return elf_path
        
    except subprocess.CalledProcessError as e:
        log_error(f"Build failed with exit code {e.returncode}")
        print(e.stdout)
        print(e.stderr)
        return None
    except subprocess.TimeoutExpired:
        log_error("Build timed out after 120 seconds")
        return None

def flash_firmware(elf_path: Path) -> bool:
    """
    Flash firmware to STM32 board using OpenOCD
    
    Args:
        elf_path: Path to the .elf file to flash
        
    Returns:
        True if successful, False otherwise
    """
    log_info(f"Flashing {elf_path.name} via OpenOCD...")
    
    try:
        result = subprocess.run(
            [
                'openocd',
                '-f', 'board/st_nucleo_f4.cfg',
                '-c', f'program {elf_path} verify reset exit'
            ],
            cwd=elf_path.parent.parent.parent.parent,  # Project root
            capture_output=True,
            text=True,
            check=True,
            timeout=30
        )
        
        log_success("Flash complete")
        return True
        
    except subprocess.CalledProcessError as e:
        log_error(f"Flash failed with exit code {e.returncode}")
        print(e.stderr)
        return False
    except subprocess.TimeoutExpired:
        log_error("Flash timed out after 30 seconds")
        return False

def find_serial_port() -> Optional[str]:
    """
    Auto-detect NUCLEO serial port
    
    Returns:
        Serial port path or None if not found
    """
    # Mac patterns
    mac_patterns = ['/dev/cu.usbmodem*']
    # Linux patterns
    linux_patterns = ['/dev/ttyACM*']
    
    for pattern in mac_patterns + linux_patterns:
        ports = glob.glob(pattern)
        if ports:
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
    parser.add_argument('--junit-xml', default='hil-test-results.xml',
                        help='Path to write JUnit XML report (default: hil-test-results.xml)')

    args = parser.parse_args()

    results = {'tests': [], 'summary': {'total': 0, 'passed': 0, 'failed': 0}}
    regressions: List[Dict] = []
    exit_code = 2

    try:
        project_root = get_project_root()
        log_info(f"Project root: {project_root}")

        # Build firmware
        if not args.skip_build:
            elf_path = build_firmware(project_root)
            if not elf_path:
                return 2
        else:
            log_warning("Skipping build (--skip-build)")
            elf_path = project_root / 'build' / 'examples' / 'cli' / 'cli_simple' / 'cli_simple.elf'
            if not elf_path.exists():
                log_error("No existing ELF file found - build required")
                return 2

        # Flash firmware
        if not args.skip_flash:
            if not flash_firmware(elf_path):
                return 2
        else:
            log_warning("Skipping flash (--skip-flash)")

        # Wait for board to reset
        time.sleep(2)

        # Find serial port
        port = find_serial_port()
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
