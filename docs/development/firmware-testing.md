# Firmware and Repository Testing

This guide summarizes development boundaries and test levels. Detailed build and load steps are in
[Firmware Build and Configuration](../../firmware/build-and-config.md); canonical test commands and HIL procedures are
in the [Test Documentation Index](../tests/README.md) and [Repository Tools](../../tools/README.md).

## Design Boundaries

Keep board pin mapping separate from USB and UART transport logic. See [Architecture](../architecture.md), the
[UART subsystem design](../design/uart/README.md), [UART Pinout](../uart-pinout.md), and the
[CDC/HID overview](../design/usb/cdc-hid-overview.md) for their respective responsibilities and constraints.

## Test Scope

Validation is split across native firmware logic tests, host and cross-project contract tests, and physical HIL. The
root [pytest configuration](../../pyproject.toml) discovers repository tests; host packages and the HIL runner also have
project-specific configuration. Use the test index to choose the applicable level and command rather than treating one
suite as a substitute for another.

## CI And Hardware Scope

CI validates host behavior and firmware builds. It has no physical board or wiring fixture and cannot prove UART
signaling, USB enumeration, DMA timing, or multicore behavior under load. Physical acceptance and exact-artifact release
gates are defined in the [hardware test documentation](../tests/README.md) and [release guide](../releasing.md).
