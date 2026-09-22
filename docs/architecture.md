# Architecture

## Overview

PicoUart is a USB-to-UART bridge for RP2040 and RP2350.
The device presents 6 USB CDC interfaces to the host.
The device also presents 1 USB HID interface for status monitoring and
narrowly scoped board controls.
Each CDC interface maps to one UART channel.

## Runtime Ownership

The current firmware initializes UART backends during startup, then boots core 1
as a dedicated UART worker before core 0 starts the TinyUSB device stack and
the HID monitor. At runtime:

- core 0 performs startup initialization once, then core 1 owns runtime UART polling and reconfiguration
- core 0 services TinyUSB tasks
- core 0 moves CDC OUT traffic into shared per-port TX rings
- core 0 moves shared per-port RX ring data back to the matching CDC IN endpoint
- CDC bridge work uses bounded 1 KiB per-port batches, a 1 ms partial-buffer
  flush deadline, and rotates the first interface serviced on each poll to limit
  cross-port starvation under load
- core 0 emits a periodic HID status report when the host is ready and serves
  HID feature reads for board temperature

This means the codebase is already at the multi-port bridge stage, not the
earlier local-echo scaffold.

## Port Mapping

Each USB CDC interface maps 1:1 to one UART channel. The firmware architecture
uses 2 hardware UART backends and 4 PIO UART backends; the board GPIO allocation
is documented in [UART Pinout and Wiring](uart-pinout.md).

## Data Flow

Host application
-> USB CDC interface
-> per-port bridge logic in `usb_cdc.c`
-> UART TX/RX backend
-> target device

Each port should work independently so traffic on one UART does not block the
others more than necessary. Hardware UART ports use DMA-backed RX and TX rings.
PIO UART ports use per-port software rings with DMA-backed RX (PIO RX FIFO to
ring) and a hybrid core-1 TX path that fills the joined TX FIFO for short
queues, then uses a persistent DMA channel when deeper backlog makes that path
cheaper. Each ring has one producer and one consumer: core 0 produces TX and
consumes RX, while core 1 consumes TX and produces RX.

## Main Blocks

- USB device stack with 6 CDC ACM functions
- USB HID status-monitor function with LED-toggle, watchdog-reset, temperature, and firmware-version feature reports
- Per-port CDC-to-UART routing in the USB poll loop
- Per-port RX and TX ring buffers inside each UART backend
- 2 hardware UART backends with optional RTS/CTS backpressure
- 4 PIO UART backends
- Board-specific GPIO and peripheral mapping in `firmware/src/board/uart_board.c`
- CDC DTR is recorded for HID monitoring only and does not gate bridging; HID board controls are restricted to LED toggle and reset

## Key Behaviors

- TinyUSB is serviced from one execution context on core 0.
- UART hardware service and line-coding changes run on core 1.
- Core-to-core data movement uses per-port RX/TX rings and a small control
  mailbox.
- Hardware UARTs support the wider CDC line-coding set; PIO UARTs are 8N1-only
  and reject unsupported formats through HID `control_error` status.
- Flow control pins are assigned but disabled by default. See
  [UART Pinout and Wiring](uart-pinout.md) for pin ownership.
- HID reset is disabled by default and only compiled into trusted lab builds.
- The watchdog is petted from the USB poll loop only while the UART worker
  heartbeat is fresh.

## Detailed References

- [Ring Buffer Design](detail/ring-buffer-design.md) covers buffer ownership,
  overflow policy, DMA interaction, and HID buffer observability.
- [PIO UART Design](detail/pio-uart-design.md) covers PIO RX/TX ownership,
  hybrid TX, and PIO line-coding limits.
- [Control Plane Design](detail/control-plane-design.md) covers CDC
  line-coding requests, worker mailbox ownership, and HID error reporting.
- [CDC/HID Overview](usb/cdc-hid-overview.md) explains the relationship between the
  six CDC ports and the HID status/control interface.
- [HID Report Reference](usb/hid-report-reference.md) defines status bits,
  feature reports, and host compatibility rules.
- [Releasing PicoUart](releasing.md) defines release HIL and artifact gates.

## Known Constraints

- The compact HID report exposes high-water mark blocks and sticky overrun
  health; exact overflow counts are available through HID feature report 5.
- Sustained multi-port 1 Mbaud is bounded by USB full-speed aggregate bandwidth
  and host drain rate.
