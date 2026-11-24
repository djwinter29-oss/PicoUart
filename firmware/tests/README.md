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
- Both platforms' reset evidence, stale chip-level flags, consumed markers, and HID user/config-save reboot paths

The test sources are the detailed inventory.

Shared Pico SDK test hooks use one storage definition in [pico_sdk_hooks.c](stubs/pico_sdk_hooks.c), linked into the
native test targets by CMake. Keep hook declarations in stub headers as `extern`; weak header definitions do not
provide portable shared hook storage across ELF and Windows COFF linkers.

## Run C Tests

From the repository root:

```sh
tools/validation/run-host-tests.sh --skip-python              # add --sanitize for sanitizer builds
```

`tools/validation/validate.sh` builds the `pico` firmware first and then runs every host test, C and Python.

To configure and run the C tests directly:

```sh
cmake -S firmware/tests -B build/host-tests -G Ninja
cmake --build build/host-tests
ctest --test-dir build/host-tests --output-on-failure
```

Physical UART behavior is covered by the [HIL test documentation](../../docs/tests/README.md).
