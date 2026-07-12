# PicoUart

PicoUart is a USB-to-UART converter project for the Raspberry Pi RP2040 and RP2350.
The goal is to expose up to 6 independent USB CDC interfaces to the host, with each CDC
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
- [Ring Buffer Design](docs/detail/ring-buffer-design.md)

## Repository Layout

- [docs](docs)
- [firmware](firmware)

## Planned Architecture

Each USB CDC channel is mapped 1:1 to a UART instance:

| USB CDC | UART backend | Notes |
| --- | --- | --- |
| CDC0 | HW UART0 | Final pin mapping TBD |
| CDC1 | HW UART1 | Final pin mapping TBD |
| CDC2 | PIO UART | Final pin mapping TBD |
| CDC3 | PIO UART | Final pin mapping TBD |
| CDC4 | PIO UART | Final pin mapping TBD |
| CDC5 | PIO UART | Final pin mapping TBD |

The exact assignment between CDC indexes and physical UART pins can be adjusted later
based on board routing and firmware constraints.

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

The firmware is expected to handle:

- USB enumeration with 6 CDC ACM functions
- USB enumeration with 1 HID monitor function for status reporting
- Routing RX/TX data between each CDC interface and its matching UART
- UART configuration such as baud rate, parity, stop bits, and flow control where supported
- Buffering and scheduling so multiple active ports can run at the same time

## Design Considerations

- USB stack selection must support composite devices with multiple CDC interfaces
- PIO UART implementations need careful timing, buffering, and interrupt/DMA handling
- Aggregate throughput will depend on USB bandwidth, CPU load, and UART baud rates
- Pin planning is important because 6 UART channels require a significant number of GPIOs
- RTS/CTS support increases GPIO demand and is likely hardest on the 4 PIO-backed channels

## Current Status

This repository is currently at the planning stage.

Immediate next steps:

1. Define the USB descriptor layout for 6 CDC interfaces.
2. Assign the 2 hardware UART channels and 4 PIO UART channels to concrete GPIO pins.
3. Choose the firmware stack and SDK structure.
4. Implement and validate a single CDC-to-UART path first.
5. Scale the design to all 6 channels and verify concurrent operation.

## Possible Future Enhancements

- Per-port status LEDs
- Configurable default baud rates
- Optional RTS/CTS support on selected channels
- Vendor-specific control interface for diagnostics and statistics
- Board-specific pinout tables for RP2040 and RP2350 variants

## Summary

PicoUart aims to turn an RP2040 or RP2350 into a 6-port USB serial adapter by combining:

- 6 USB CDC interfaces on the host side
- 2 hardware UARTs
- 4 PIO-based UARTs

The result should be a flexible multi-UART bridge for development and automated test use.