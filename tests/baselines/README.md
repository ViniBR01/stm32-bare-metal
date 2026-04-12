# Performance Baselines

This directory contains baseline performance metrics for hardware-in-the-loop (HIL) tests.

## Overview

Baselines are stored in JSON format and used by the HIL test runner (`scripts/run_hil_tests.py`) to detect performance regressions. Each baseline includes:

- **Expected metric values** (e.g., throughput, cycle counts)
- **Tolerance threshold** (typically ±10%)
- **Metadata** (last updated date, test notes)

## File Structure

### `performance.json`

Main baseline file containing performance metrics for all HIL tests.

**Format:**
```json
{
  "test_name": {
    "metric_name": expected_value,
    "cycles": expected_cycles,
    "tolerance_percent": 10,
    "last_updated": "2026-04-12",
    "notes": "Description of test parameters"
  }
}
```

**Fields:**
- `metric_name` - Performance metric value (e.g., `throughput_kbps`)
- `cycles` - DWT cycle count for the operation
- `tolerance_percent` - Allowed variation from baseline (default: 10%)
- `last_updated` - ISO date when baseline was last updated
- `notes` - Human-readable description of test configuration

## Updating Baselines

### When to Update

Update baselines when:
1. **Driver optimization** - Legitimate performance improvement
2. **Clock configuration change** - Affects all timing-dependent tests
3. **Algorithm change** - New implementation with different performance characteristics
4. **Hardware change** - Different board or peripheral configuration

### How to Update

1. **Run tests and capture metrics:**
   ```bash
   python3 scripts/run_hil_tests.py
   ```

2. **Review the output** to see actual measured values:
   ```
   TEST:spi_perf_polled_3bytes:PASS:cycles=1234:throughput_kbps=567
   TEST:spi_perf_dma_256bytes:PASS:cycles=8900:throughput_kbps=890
   ```

3. **Update `performance.json`** with the new values:
   ```json
   {
     "spi_perf_polled_3bytes": {
       "throughput_kbps": 567,
       "cycles": 1234,
       "tolerance_percent": 10,
       "last_updated": "2026-04-12",
       "notes": "SPI2, prescaler=4, polled mode, 3-byte transfer"
     }
   }
   ```

4. **Commit the change with justification:**
   ```bash
   git add tests/baselines/performance.json
   git commit -m "Update SPI perf baselines after DMA optimization
   
   - Reduced DMA transfer overhead by 15%
   - New baseline: 890 KB/s (was 775 KB/s)
   - Measured across 10 runs, stable within ±5%"
   ```

### Baseline Validation

The HIL runner validates metrics against baselines:

```
✓ spi_perf_polled_3bytes: Cycles OK (1234)
✓ spi_perf_polled_3bytes: throughput_kbps OK (567)
✗ spi_perf_dma_256bytes: throughput_kbps regression
    Expected: 890 ±10%
    Actual: 650
```

**Exit codes:**
- `0` - All tests passed and within baseline thresholds
- `1` - Test failures or performance regressions detected
- `2` - Error (build/flash/serial failure)

## Null Values

Tests with `null` baseline values will not trigger validation failures:

```json
{
  "new_test": {
    "throughput_kbps": null,
    "cycles": null,
    "tolerance_percent": 10,
    "last_updated": null,
    "notes": "New test - baseline not yet established"
  }
}
```

This allows adding new tests incrementally without breaking CI.

## PR Review Process

When updating baselines in a PR:

1. **Explain why** the baseline changed (driver optimization, bug fix, etc.)
2. **Show measurements** - multiple runs demonstrating stability
3. **Justify tolerance** - wider tolerances may be needed for environmental variance
4. **Document trade-offs** - if one metric improves at the expense of another

Baseline updates without justification should be questioned during code review.

## Future Enhancements

Planned improvements (not yet implemented):

- **Trending dashboard** - Graph metrics over time
- **Multiple baseline sets** - Per-board or per-environment baselines
- **Statistical analysis** - Track variance and detect outliers
- **Automatic baseline suggestion** - Runner suggests updates based on stable measurements
