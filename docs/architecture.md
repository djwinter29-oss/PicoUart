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
- CDC bridge work uses bounded 1 KiB per-port batches, a 1 ms partial-buffer
  flush deadline, and rotates the first interface serviced on each poll to limit
  cross-port starvation under load
- core 0 emits a periodic HID status report when the host is ready and serves
  HID feature reads for board temperature

This means the codebase is already at the multi-port bridge stage, not the earlier local-echo scaffold.

## Port Mapping

| USB CDC | UART Type | Notes |
| --- | --- | --- |
| CDC0 | Hardware UART0 | TX GP0, RX GP1; RTS GP3 / CTS GP2 reserved (FC off by default) |
| CDC1 | Hardware UART1 | TX GP4, RX GP5; RTS GP7 / CTS GP6 reserved (FC off by default) |
| CDC2 | PIO UART | TX GP8, RX GP9; RTS GP10 / CTS GP11 opt-in |
| CDC3 | PIO UART | TX GP12, RX GP13; RTS GP14 / CTS GP15 opt-in |
| CDC4 | PIO UART | TX GP16, RX GP17; RTS GP18 / CTS GP19 opt-in |
| CDC5 | PIO UART | TX GP20, RX GP21; RTS GP22 / CTS GP26 opt-in |

## Data Flow

Host application
-> USB CDC interface
-> per-port bridge logic in `usb_cdc.c`
-> UART TX/RX backend
-> target device

Each port should work independently so traffic on one UART does not block the others more than necessary.
Hardware UART ports use DMA-backed RX and TX rings. PIO UART ports use per-port software rings with
DMA-backed RX (PIO RX FIFO → ring) and a hybrid core-1 TX path that fills the joined TX FIFO for short
queues, then uses a persistent DMA channel when deeper backlog makes that path cheaper. Each ring has one producer and
one consumer: core 0 produces TX and consumes RX, while core 1 consumes TX and produces RX.

## Main Blocks

- USB device stack with 6 CDC ACM functions
- USB HID status-monitor function with LED-toggle, watchdog-reset, temperature, and firmware-version feature reports
- Per-port CDC-to-UART routing in the USB poll loop
- Per-port RX and TX ring buffers inside each UART backend
- 2 hardware UART backends with optional RTS/CTS backpressure
- 4 PIO UART backends
- Board-specific GPIO and peripheral mapping in `firmware/src/config/uart_board.c`
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
- Hardware UART0/UART1 keep RTS/CTS disabled by default. When enabled in the
  board configuration, CTS remains a hardware TX input and RTS is driven from
  RX-ring occupancy with hysteresis. PIO RX RTS and CTS TX gating are similarly
  opt-in; CTS pauses the PIO state machine before starting the next frame.
- PIO UART ports support 8N1 with stop-bit framing validation; hardware UART ports additionally apply valid CDC data-bit,
  stop-bit, and parity settings.
- Deferred line-coding applies fail with `CONTROL_ERROR` if the backend cannot reach a
  safe idle boundary within 1 second (avoids pausing USB ingress indefinitely).
- When core 0 accepts a line-coding request, it snapshots the TX-ring producer
  sequence and stops new CDC OUT ingress for that port. Core 1 drains exactly
  that captured old-format backlog before it waits for DMA/FIFO/shifter idle
  and applies the new format. Bytes that arrive after the request remain in
  USB until the new format is active.
- PIO transitions require an empty RX FIFO and the RX state machine to be at
  its `wait for start bit` instruction, proving it completed the prior frame.
  Hardware UART transitions re-check only the RX FIFO because PL011 exposes no
  RX-shifter-idle bit. A peer that starts a frame during the forced hardware
  DMA/peripheral restart can lose that frame; use a peer-level pause or RTS/CTS
  when loss is not acceptable.
- A posted mailbox command keeps USB-to-UART ingress paused until core 1 either
  accepts it for deferred application or completes/rejects it; repeated host
  requests cannot reopen ingress during that ownership handoff.
- CDC `SET_LINE_CODING` can succeed at the USB layer while firmware rejects the request;
  hosts must watch HID health bit 2 (`CONTROL_ERROR`). Shared validation lives in
  `firmware/src/uart/line_coding.c` (50–3 000 000 baud). PIO also rejects bauds its
  clock divider cannot represent (fail-fast, no 1 s pending window).
- Hardware UART rates are accepted only when the PL011 divisor is representable
  from `clk_peri` within 2% error. The driver stores and reports the actual
  SDK-programmed rate after a successful transition.
- USB product string `PicoUart CDC+HID PIO 8N1` and CDC2–CDC5 interface strings
  advertise the PIO 8N1 limit; TinyUSB still cannot STALL `SET_LINE_CODING`.
- Hardware UART RX DMA re-arms from a DMA IRQ when the transfer counter exhausts; the
  worker poll path is a safety net. Line-format restarts continue DMA at the live ring
  producer index after publishing all bytes accepted before DMA stops. With HW FC off
  (default), a sustained peer flood can still overrun the UART FIFO / ring — exercise
  that case in HIL before advertising flow control.
- HID reset is **disabled by default**. Compile with `-DPICO_UART_ALLOW_HID_RESET=1`
  to enable arm (`3`) then reset (`2`) within 2 s. Enabled builds advertise that
  capability in HID board-status `reserved0` bit 0; the host `reset` command
  fails closed when the bit is clear.
- After USB and UART init, core 0 arms an 8 s watchdog (`pause_on_debug`) and
  pets it from the USB poll loop only while the UART worker heartbeat is fresh
  (2 s stale window). A wedged TinyUSB/bridge loop or a silent core 1 resets;
  a debugger can still inspect `isr_hardfault`.
- Production builds use the rated 125 MHz RP2040 or 150 MHz RP2350 system
  clock. `tools/linux/build.sh --unsafe-overclock --system-clock-khz ...` is
  required for another clock and is intended only for recorded qualification.

## Open Items

- Whether full ring occupancy should be added to the compact HID report
  (high-water mark blocks, sticky overrun health, and exact overflow counts are already present)
- Commercial derivatives must replace the lab USB identity (`cafe:4010`); this
  project publishes artifacts under that unallocated identity (see `docs/releasing.md`)
- Sustained multi-port 1 Mbaud remains bounded by USB full-speed aggregate bandwidth
