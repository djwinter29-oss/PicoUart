# Native Firmware Tests

This directory contains native C tests for firmware logic that can run without a Pico board. The tests use Unity, CMake,
and CTest; they do not exercise the TinyUSB device stack, live DMA IRQs, or physical UART wiring.

## Run

From the repository root, build and run the native C tests with:

```sh
tools/validation/run-host-tests.sh --skip-python
```

This needs a native C toolchain, CMake, and either Ninja or Make. The
[hardware-free test checklist](../../docs/tests/README.md#without-hardware) also runs the Python suite and firmware
builds.

## What Is Tested

Each `test_<module>.c` file covers one firmware module, from ring buffers and line-coding policy to the CDC, HID,
control-plane, DMA, and UART backend logic. The file names are the inventory.

Shared Pico SDK test hooks use one storage definition in [pico_sdk_hooks.c](stubs/pico_sdk_hooks.c), linked into the
native test targets by CMake. Keep hook declarations in stub headers as `extern`; weak header definitions do not
provide portable shared hook storage across ELF and Windows COFF linkers.

Use the [test overview](../../docs/tests/README.md) to choose further hardware-free checks or HIL.
