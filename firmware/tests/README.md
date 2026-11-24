# Firmware Host Tests

This directory contains native C tests for firmware logic that can run without a Pico board. The tests use Unity, CMake,
and CTest; they do not exercise the TinyUSB device stack, live DMA IRQs, or physical UART wiring.

## What Is Tested

The native test suite covers:

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

Shared Pico SDK test hooks use one storage definition in [pico_sdk_hooks.c](stubs/pico_sdk_hooks.c), linked into the
native test targets by CMake. Keep hook declarations in stub headers as `extern`; weak header definitions do not
provide portable shared hook storage across ELF and Windows COFF linkers.

Use the [hardware-free checks](../../docs/tests/README.md#without-hardware) for validation commands; the
[test overview](../../docs/tests/README.md#with-hardware) covers HIL.
