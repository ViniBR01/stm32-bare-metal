# Testing

## Overview

Tests are split into two categories:

| Type | Location | Toolchain | Runs on |
|---|---|---|---|
| Host unit tests | `tests/` | Native `gcc` | Any Linux/macOS host, CI |
| Hardware-in-the-loop (HIL) | _(future)_ | `arm-none-eabi-gcc` | Raspberry Pi + NUCLEO board |

## Host Unit Tests

### Framework

[Unity](https://github.com/ThrowTheSwitch/Unity) v2.6.2 — lightweight C unit test framework designed for embedded systems.

Unity source lives at `3rd_party/log_c/3rd-party/unity/src/` (nested submodule).
> Issue #84 tracks moving Unity to a direct root-level submodule at `3rd_party/unity/`.

### Running tests

```sh
make test        # Build and run all host test suites
```

### Test suites

| Suite | Location | Tests | What is covered |
|---|---|---|---|
| `string_utils` | `tests/string_utils/` | 23 | Custom string functions in `utils/src/string_utils.c` |
| `cli` | `tests/cli/` | 41 | CLI engine in `utils/src/cli.c` |
| **Total** | | **64** | |

### Architecture

Each suite is a standalone executable compiled with native `gcc`:

```
tests/<suite>/
├── test_<suite>.c      # Unity test file (setUp, tearDown, RUN_TEST macros)
├── stubs/              # Header stubs for embedded-only includes (printf.h, printf_dma.h)
└── Makefile            # Compiles test + source under test + unity.c → test_<suite>.out
```

Stubs allow source files that `#include "printf.h"` or `#include "printf_dma.h"` to compile on the host without the full embedded stack.

### Adding a new test suite

1. Create `tests/<module>/` with a `Makefile` modelled on an existing suite
2. Add stubs for any embedded-only headers the module depends on
3. Add the new suite directory to `SUBDIRS` in `tests/Makefile`
4. Write tests following the Unity pattern: `setUp()`, `tearDown()`, `RUN_TEST(test_fn)` in `main()`

### Test output format

Unity outputs one line per test:
```
tests/cli/test_cli.c:45:test_cli_init_registers_commands:PASS
tests/cli/test_cli.c:46:test_process_char_adds_printable:PASS
...
23 Tests 0 Failures 0 Ignored
OK
```

Exit code is non-zero on any test failure, which fails the CI job.

## CI Integration

`make test` is the entry point for CI. See [ci.md](ci.md) for the full pipeline.

Planned improvements:
- ~~Issue #85: JUnit XML output~~ — **Done**: `tests/unity_to_junit.py` converts Unity stdout to JUnit XML; `dorny/test-reporter@v3` publishes a Test Summary tab on every PR
- Issue #88: gcov/lcov coverage reporting uploaded as CI artifact
