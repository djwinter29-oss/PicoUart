# PicoUart

PicoUart is a six-channel USB-to-UART bridge for Raspberry Pi RP2040 and RP2350
boards. Each USB CDC interface maps to one UART channel on the target.

## What It Provides

- 6 USB CDC ACM interfaces presented to the host PC
- 1 USB HID status-monitor interface presented to the host PC
- 6 UART channels on the device side
- 2 UARTs implemented with RP2040/RP2350 hardware UART peripherals
- 4 UARTs implemented with PIO-based software UARTs
- RTS/CTS pins assigned for future or explicit flow-control testing, disabled
  by default

This makes the board act like a six-port USB serial converter using a
low-cost microcontroller platform.
## Using PicoUart

Install and use the host client, CLI, or local diagnostics dashboard from the
[PicoUart Python guide](host/python/README.md). Before connecting target
hardware, check the [UART pinout and wiring](docs/uart-pinout.md) and the
[CDC/HID overview](docs/usb/cdc-hid-overview.md).

## Supported Hardware

- RP2040-based boards such as Raspberry Pi Pico (`--board pico`)
- RP2350-based boards such as Raspberry Pi Pico 2 (`--board pico2`)

## Important Limitations

- PIO UART channels support 8N1 only; unsupported line coding is rejected.
- RTS/CTS flow control is disabled by default. Host CDC RTS is ignored, and
  DTR is monitored but does not gate UART traffic.
- Sustained multi-port throughput is limited by USB full-speed bandwidth and
  host drain rate.
- `cafe:4010` is a development/lab USB identity, not for commercial derivatives;
  see [SECURITY.md](SECURITY.md).

## Documentation

- [Host Python installation and usage](host/python/README.md)
- [UART pinout and wiring](docs/uart-pinout.md)
- [CDC/HID behavior](docs/usb/cdc-hid-overview.md)
- [HID report reference](docs/usb/hid-report-reference.md)
- [Hardware test index](docs/tests/README.md)
- [Release policy and qualification](docs/releasing.md)
- [Firmware development and repository testing](docs/development/firmware-testing.md)
- [Host Python package development](docs/development/host-python.md)
- [Firmware architecture](docs/architecture.md)
