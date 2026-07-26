# Architecture

## Overview

PicoUart is a USB-to-UART bridge for RP2040 and RP2350.
The device presents 6 USB CDC interfaces to the host.
The device also presents 1 USB HID interface for status monitoring and
narrowly scoped board controls.
Each CDC interface maps to one UART channel.

## Current Firmware Status

The current firmware initializes UART backends during startup, then boots core 1
as a dedicated UART worker before core 0 starts the TinyUSB device stack and
the HID monitor. At runtime:

- core 0 performs startup initialization once, then core 1 owns runtime UART polling and reconfiguration
- core 0 services TinyUSB tasks
- core 0 moves CDC OUT traffic into shared per-port TX rings
- core 0 moves shared per-port RX ring data back to the matching CDC IN endpoint
- core 0 emits a periodic HID status report when the host is ready and serves
  HID feature reads for board temperature

This means the codebase is already at the multi-port bridge stage, not the earlier local-echo scaffold.

## Port Mapping

| USB CDC | UART Type | Notes |
| --- | --- | --- |
| CDC0 | Hardware UART0 | TX GP0, RX GP1, RTS GP3, CTS GP2 |
| CDC1 | Hardware UART1 | TX GP4, RX GP5, RTS GP7, CTS GP6 |
| CDC2 | PIO UART | TX GP8, RX GP9, RTS GP10, CTS GP11 |
| CDC3 | PIO UART | TX GP12, RX GP13, RTS GP14, CTS GP15 |
| CDC4 | PIO UART | TX GP16, RX GP17, RTS GP18, CTS GP19 |
| CDC5 | PIO UART | TX GP20, RX GP21, RTS GP22, CTS GP26 |

## Data Flow

Host application
-> USB CDC interface
-> per-port bridge logic in `usb_cdc.c`
-> UART TX/RX backend
-> target device

Each port should work independently so traffic on one UART does not block the others more than necessary.
Hardware UART ports use DMA-backed RX and TX rings. PIO UART ports use per-port software rings with
DMA-backed RX (PIO RX FIFO → ring) and a hybrid core-1 TX path that fills the joined TX FIFO for short
queues, then lazily claims DMA when deeper backlog makes that path cheaper. Each ring has one producer and
one consumer: core 0 produces TX and consumes RX, while core 1 consumes TX and produces RX.

## Main Blocks

- USB device stack with 6 CDC ACM functions
- USB HID status-monitor function with LED-toggle, watchdog-reset, temperature, and firmware-version feature reports
- Per-port CDC-to-UART routing in the USB poll loop
- Per-port RX and TX ring buffers inside each UART backend
- 2 hardware UART backends with RTS/CTS pins exposed but disabled by default
- 4 PIO UART backends
- Board-specific GPIO configuration
- CDC DTR is recorded for HID monitoring only and does not gate bridging; HID board controls are restricted to LED toggle and reset

## Design Notes

- Use TinyUSB for the multi-CDC USB device layer.
- Use Pico SDK for platform support.
- Keep pin mapping separate from bridge logic.
- Use separate RX and TX ring buffers per logical port.
- Use DMA for hardware UART RX and TX where the silicon already supports it well.
- Keep TinyUSB ownership in one execution context by polling it from `main`.
- Keep UART-controller ownership on core 1; cross-core traffic uses only the
  per-port rings and the control mailbox.
- Core 1 also installs and services the DMA RX re-arm IRQ handlers. Core 0
  configures backends during startup but does not execute live UART IRQ work.
- Hardware UART0/UART1 leave RTS/CTS disabled by default. The pins stay available
  for explicit future flow-control enablement, but the default bench wiring and
  Debug Probe validation use TX, RX, and GND only.
- PIO UART ports support 8N1 with stop-bit framing validation; hardware UART ports additionally apply valid CDC data-bit,
  stop-bit, and parity settings.
- Deferred line-coding applies fail with `CONTROL_ERROR` if the backend cannot reach a
  safe idle boundary within 1 second (avoids pausing USB ingress indefinitely).
- A posted mailbox command keeps USB-to-UART ingress paused until core 1 either
  accepts it for deferred application or completes/rejects it; repeated host
  requests cannot reopen ingress during that ownership handoff.
- CDC `SET_LINE_CODING` can succeed at the USB layer while firmware rejects the request;
  hosts must watch HID health bit 2 (`CONTROL_ERROR`). Shared validation lives in
  `firmware/src/uart/line_coding.c` (50–3 000 000 baud). PIO also rejects bauds its
  clock divider cannot represent (fail-fast, no 1 s pending window).
- Hardware UART RX DMA re-arms from a DMA IRQ when the transfer counter exhausts; the
  worker poll path is a safety net. Line-format restarts continue DMA at the live ring
  producer index after publishing all bytes accepted before DMA stops. Peers that ignore RTS can still overrun the UART FIFO under sustained
  flood - exercise that case in HIL before advertising flow control.
- HID reset requires arm (`3`) then reset (`2`) within 2 s, or compile with
  `PICO_UART_ALLOW_HID_RESET=0` to disable it.

## Open Items

- RTS and CTS runtime behavior for PIO UART ports
- Whether full ring occupancy/overflow counters should be added to the compact HID report
  (high-water mark blocks and a sticky overrun health bit are already present)
- Replace development USB IDs (`cafe:4010`) before production releases (see `docs/releasing.md`)
- Sustained multi-port 1 Mbaud remains bounded by USB full-speed aggregate bandwidth
