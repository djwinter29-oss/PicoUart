# Firmware Host Tests

This directory contains native C tests for firmware logic that can run without
a Pico board. The tests use Unity, CMake, and CTest; they do not exercise the
TinyUSB device stack, live DMA IRQs, or physical UART wiring.

## What Is Tested

- Ring-buffer occupancy, wrapping, snapshots, and overflow recovery
- UART line-coding and topology policy
- DMA progress, TXSTALL timing, and backend policy helpers
- CDC soft-pending and control-ownership rules
- Hardware/PIO resource claim rollback
- LED policy behavior

The test source files are the detailed inventory. The production design notes
are linked from the root [README](../../README.md).

## Run C Tests

From the repository root:

```sh
tools/test/test-host.sh --skip-python
```

For a combined firmware build and host-test run:

```sh
tools/test/check.sh --skip-build
```

For sanitizer coverage:

```sh
tools/test/test-host.sh --sanitize --skip-python
```

To configure and run the C tests directly:

```sh
cmake -S firmware/tests -B build/host-tests -G Ninja
cmake --build build/host-tests
ctest --test-dir build/host-tests --output-on-failure
```

Firmware HIL and physical UART behavior are documented in
[docs/tests](../../docs/tests) and [docs/releasing.md](../../docs/releasing.md).
Python host-tool tests live under [host/python/tests](../../host/python/tests)
and use [host/python/pyproject.toml](../../host/python/pyproject.toml).
