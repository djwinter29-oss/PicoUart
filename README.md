# PicoUart

PicoUart is a six-channel USB-to-UART bridge for Raspberry Pi RP2040 and RP2350 boards. Each USB CDC interface maps to
one UART channel on the target.

## Why PicoUart

Embedded bring-up often means watching several UARTs at once: a boot log, a co-processor, and a debug console, for
example. PicoUart explores using one Pico-class MCU to expose six independent serial links over USB, with a separate HID
interface for health diagnostics. The goal is a compact, inspectable tool for multi-port development without a stack of
separate USB-UART adapters.

## At a Glance

| Interface   | Role                                                               | Implementation                    |
| ----------- | ------------------------------------------------------------------ | --------------------------------- |
| CDC0–CDC5   | Six independent host serial ports                                  | One-to-one mapping to UART0–UART5 |
| HID         | Health, overflow, version, temperature, and limited board controls | Separate from UART data           |
| UART0–UART1 | General UART traffic                                               | RP2040/RP2350 hardware UARTs      |
| UART2–UART5 | General UART traffic                                               | PIO UARTs; 8N1 only               |

## Using PicoUart

Install and use the host client, CLI, or local diagnostics dashboard from the
[PicoUart Python guide](host/python/README.md). Before connecting target hardware, check the
[UART pinout and wiring](docs/uart-pinout.md) and the [CDC/HID overview](docs/usb/cdc-hid-overview.md).

## Supported Hardware

- RP2040-based boards such as Raspberry Pi Pico (`--board pico`)
- RP2350-based boards such as Raspberry Pi Pico 2 (`--board pico2`)

## Important Limitations

- PIO UART channels support 8N1 only; unsupported line coding is rejected.
- RTS/CTS flow control is disabled by default. Host CDC RTS is ignored, and DTR is monitored but does not gate UART
  traffic.
- Sustained multi-port throughput is limited by USB full-speed bandwidth and host drain rate.
- `cafe:4010` is a development/lab USB identity, not for commercial derivatives; see [SECURITY.md](SECURITY.md).

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
