# PicoUart

PicoUart is a USB-to-UART converter project for the Raspberry Pi RP2040 and RP2350.
The current firmware exposes 6 independent USB CDC interfaces to the host, with each CDC
interface mapped to one UART channel on the target side.

## What It Provides

- 6 USB CDC ACM interfaces presented to the host PC
- 1 USB HID status-monitor interface presented to the host PC
- 6 UART channels on the device side
- 2 UARTs implemented with RP2040/RP2350 hardware UART peripherals
- 4 UARTs implemented with PIO-based software UARTs
- RTS/CTS pins assigned for future or explicit flow-control testing, disabled
  by default

This makes the board act like a 6-port USB serial converter while still using a low-cost
microcontroller platform.

## Documentation

Start here:

- [Architecture](docs/architecture.md)
- [UART Pinout and Wiring](docs/uart-pinout.md)
- [CDC/HID Overview](docs/usb/cdc-hid-overview.md)

Hardware testing:

- [Self-Test Setup](docs/tests/self-test-setup.md)
- [Functional Test Plan](docs/tests/functional-test-plan.md)
- [Performance Test Plan](docs/tests/performance-test-plan.md)
- [Performance Test Results](docs/tests/performance-test-results.md)

Release and policy:

- [Releasing](docs/releasing.md)
- [Security / USB identity policy](SECURITY.md)

Design notes:

- [HID Report Reference](docs/usb/hid-report-reference.md)
- [Control Plane Design](docs/detail/control-plane-design.md)
- [Ring Buffer Design](docs/detail/ring-buffer-design.md)
- [PIO UART Design](docs/detail/pio-uart-design.md)

When firmware behavior changes, update the matching design or test document in
the same change. This keeps the detailed docs from drifting away from code.

## Quick Start

Firmware development, flashing, release builds, and physical HIL are supported on
Ubuntu/Linux. Windows remains supported for host-side Python HID and CDC tools;
use WSL2 Ubuntu for firmware work from Windows.

```sh
. tools/firmware/setup-sdk-env.sh --sdk-version 2.3.0
tools/firmware/build.sh --board pico
tools/firmware/build.sh --board pico2
tools/test/test-host.sh
```

CI does not provide a Pico/Pico 2 board, Debug Probe, or jumper-wire fixture,
so physical UART, USB, HID, and performance tests cannot run automatically in
the pipeline. Users must assemble the hardware fixture and run the documented
tests locally. Start with [Self-Test Setup](docs/tests/self-test-setup.md),
then follow the [Functional Test Plan](docs/tests/functional-test-plan.md) and
[Performance Test Plan](docs/tests/performance-test-plan.md). Record results
in [Performance Test Results](docs/tests/performance-test-results.md).

CI builds both firmware targets on Linux and runs host tests on Linux and
Windows. Release tags open a draft GitHub Release; publish only after the gates
in [Releasing](docs/releasing.md).

## Repository Layout

- [docs](docs)
- [firmware](firmware) - Pico SDK firmware project; see [build and configuration](firmware/build-and-config.md)
- [host/python](host/python) - Python HID monitor and board-control utility (`src/`, tests in `tests/`)
- [tools](tools) - repo tooling grouped by firmware, hardware/HIL, release, and test helpers

## Target Devices

- RP2040-based boards such as Raspberry Pi Pico (`--board pico`)
- RP2350-based boards such as Raspberry Pi Pico 2 (`--board pico2`)

CI builds both targets. The transport model stays consistent across both families.

## Current Status

The firmware currently provides:

- USB enumeration with 6 CDC ACM functions
- 1 vendor HID interface for status and limited board control
- Routing RX/TX data between each CDC interface and its matching UART
- CDC line-coding updates, with unsupported requests reported through HID health
- DMA-backed hardware UART paths
- DMA-backed PIO RX and hybrid FIFO/DMA PIO TX

Known gaps in the current implementation:

- Host CDC RTS is ignored; DTR is recorded for HID monitoring only.
- PIO UART ports remain 8N1-only and reject unsupported parity, stop-bit, or
  data-bit changes.
- Runtime RTS/CTS flow control is opt-in and must be explicitly configured and
  tested before claiming lossless behavior.
- Sustained multi-port 1 Mbaud is limited by USB full-speed aggregate bandwidth.

## Possible Future Enhancements

- Per-port status LEDs
- Configurable default baud rates
- PIO RTS/CTS qualification and tuning
- Replace development USB IDs (`cafe:4010`) with an allocated identity (see [SECURITY.md](SECURITY.md))
