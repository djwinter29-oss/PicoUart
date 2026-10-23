# Firmware Host Tests

This directory contains native C tests for firmware logic that can run without a Pico board. The tests use Unity, CMake,
and CTest; they do not exercise the TinyUSB device stack, live DMA IRQs, or physical UART wiring.

## What Is Tested

- Ring-buffer occupancy, wrapping, snapshots, and overflow recovery
- UART line-coding and topology policy
- DMA progress, TXSTALL timing, and backend policy helpers
- CDC soft-pending and control-ownership rules
- Bridge partial/zero writes, wrapped spans, snapshot sequence changes, and overflow during retirement
- HID ring-peak saturation at the version-1 report limit
- Logical-port RX alignment, independent storage, and compact metadata layout
- Hardware/PIO resource claim rollback
- LED policy behavior

The test source files are the detailed inventory. The production design notes are linked from the root
[README](../../README.md).

Shared Pico SDK test hooks use one storage definition in [pico_sdk_hooks.c](stubs/pico_sdk_hooks.c), linked into the
native test targets by CMake. Keep hook declarations in stub headers as `extern`; weak header definitions do not
provide portable shared hook storage across ELF and Windows COFF linkers.

## Run C Tests

From the repository root:

```sh
tools/validation/run-host-tests.sh --skip-python
```

For a combined firmware build and host-test run:

```sh
tools/validation/validate.sh --skip-build
```

For sanitizer coverage:

```sh
tools/validation/run-host-tests.sh --sanitize --skip-python
```

To configure and run the C tests directly:

```sh
cmake -S firmware/tests -B build/host-tests -G Ninja
cmake --build build/host-tests
ctest --test-dir build/host-tests --output-on-failure
```

Firmware HIL and physical UART behavior are documented in [docs/tests](../../docs/tests) and
[docs/deployment/releasing.md](../../docs/deployment/releasing.md). Python package tests live under
[host/python/tests](../../host/python/tests). Repository-level Python tests live under [tests](../../tests) and are
discovered with the repository [pyproject.toml](../../pyproject.toml).
