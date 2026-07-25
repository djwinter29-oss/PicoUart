# PicoUart

PicoUart is a USB-to-UART converter project for the Raspberry Pi RP2040 and RP2350.
The current firmware exposes 6 independent USB CDC interfaces to the host, with each CDC
interface mapped to one UART channel on the target side.

## Project Goal

This project is intended to provide a compact multi-port USB serial adapter built on the
Pico platform:

- 6 USB CDC ACM interfaces presented to the host PC
- 1 USB HID status-monitor interface presented to the host PC
- 6 UART channels on the device side
- 2 UARTs implemented with RP2040/RP2350 hardware UART peripherals
- 4 UARTs implemented with PIO-based software UARTs
- RTS/CTS hardware flow-control signals included in the intended channel design

This makes the board act like a 6-port USB serial converter while still using a low-cost
microcontroller platform.

## Documentation

- [Architecture](docs/architecture.md)
- [Hardware Wiring](docs/hardware-wiring.md)
- [HID Monitor and Board Control](docs/hid-monitor.md)
- [Test Connections](docs/test-connections.md)
- [Ring Buffer Design](docs/detail/ring-buffer-design.md)
- [Security / USB identity policy](SECURITY.md)

## Build and CI

Linux:

```sh
. tools/linux/setup-sdk-env.sh
tools/linux/build.sh --board pico
tools/linux/build.sh --board pico2
```

Pull requests run `.github/workflows/pr-check.yml` (firmware build for `pico` /
`pico2`, host C Unity tests, Python pytest, plus host-tool syntax checks). Pushing a tag matching `v*` (for example
`v1.2.3`) runs `.github/workflows/release.yml`, which builds both boards with
version `1.2.3` stamped into binary info and HID, sets USB `bcdDevice` to
major.minor BCD (`0x0102` for `1.2.3`), and publishes a GitHub Release with
board-qualified artifacts (`pico_uart-v1.2.3-pico.uf2`,
`pico_uart-v1.2.3-pico2.uf2`, and the matching `.elf` / `.bin` / `.hex` files).
After flashing, `python3 host/python/src/pico_uart_hid.py version` should print
`1.2.3`.

Host-side tests (no board required):

```sh
tools/linux/test-host.sh
```

See [`firmware/tests/README.md`](firmware/tests/README.md).

## Repository Layout

- [docs](docs)
- [firmware](firmware)
- [host/python](host/python) - Python HID monitor and board-control utility (`src/`, tests in `tests/`)

## Current Architecture

Each USB CDC channel is mapped 1:1 to a UART instance:

| USB CDC | UART backend | TX | RX |
| --- | --- | --- | --- |
| CDC0 | HW UART0 | GP0 | GP1 |
| CDC1 | HW UART1 | GP4 | GP5 |
| CDC2 | PIO UART | GP8 | GP9 |
| CDC3 | PIO UART | GP12 | GP13 |
| CDC4 | PIO UART | GP16 | GP17 |
| CDC5 | PIO UART | GP20 | GP21 |

The current transport code only uses TX and RX. The board-level RTS and CTS reservations are
documented separately and remain a follow-up item.

## Target Devices

- RP2040-based boards such as Raspberry Pi Pico
- RP2350-based boards as the design evolves

The firmware should keep the transport model consistent across both device families even if
the low-level implementation differs.

## Core Features

- Multi-port USB CDC device
- Up to 6 simultaneous UART channels
- Mix of hardware UART and PIO UART implementations
- Independent data path for each USB-to-UART pair
- Intended for test fixtures, embedded bring-up, and multi-device debug setups

## Firmware Scope

The firmware currently handles:

- USB enumeration with 6 CDC ACM functions
- USB enumeration with 1 HID monitor function for status reporting
- Routing RX/TX data between each CDC interface and its matching UART
- UART configuration updates received through USB CDC line coding
- Buffering and scheduling so multiple active ports can run at the same time

The HID interface publishes a compact per-port status report, cumulative PIO
statistics, and the internal temperature estimate. It also supports narrowly
scoped board commands to toggle the default LED and reset the board; it does
not configure UART transport settings.

## Design Considerations

- USB stack selection must support composite devices with multiple CDC interfaces
- PIO UART implementations need careful timing, buffering, and interrupt/DMA handling
- Aggregate throughput will depend on USB bandwidth, CPU load, and UART baud rates
- Pin planning is important because 6 UART channels require a significant number of GPIOs
- RTS/CTS support increases GPIO demand and is likely hardest on the 4 PIO-backed channels

## Current Status

The firmware has moved beyond the planning stage and now provides a working baseline bridge:

1. 6 CDC ACM interfaces enumerate through TinyUSB.
2. 1 vendor HID interface enumerates for status monitoring and limited board control.
3. CDC traffic is bridged to 2 hardware UART backends and 4 PIO UART backends.
4. Hardware UART ports use DMA-backed RX and TX rings.
5. PIO UART ports use polling with per-port RX and TX rings.

Known gaps in the current implementation:

1. PIO UART RTS/CTS pins are reserved but have no runtime flow-control behavior yet (hardware UART0/UART1 enable RTS/CTS).
2. Host CDC RTS is ignored; DTR is recorded for HID monitoring only and does not gate bridging.
3. PIO UART ports remain 8N1-only and reject unsupported parity, stop-bit, or data-bit changes.
4. HID exposes ring high-water marks and a sticky RX-overrun health bit; full occupancy/overflow **counts** are not in the compact HID report.

## Possible Future Enhancements

- Per-port status LEDs
- Configurable default baud rates
- PIO UART RTS/CTS runtime flow control
- Board-specific pinout tables for RP2040 and RP2350 variants
- Replace development USB IDs (`cafe:4010`) with an allocated identity (see [SECURITY.md](SECURITY.md))

## Summary

PicoUart turns an RP2040 or RP2350 into a 6-port USB serial adapter by combining:

- 6 USB CDC interfaces on the host side
- 2 hardware UARTs
- 4 PIO-based UARTs

The result should be a flexible multi-UART bridge for development and automated test use.
