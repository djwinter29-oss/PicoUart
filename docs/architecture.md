# Architecture

## Overview

PicoUart is a USB-to-UART bridge for RP2040 and RP2350.
The device presents 6 USB CDC interfaces to the host.
The device also presents 1 USB HID interface for status monitoring and
narrowly scoped board controls.
Each CDC interface maps to one UART channel.

## Current Firmware Status

The current firmware boots core 1 as a dedicated UART worker, then core 0 initializes
the TinyUSB device stack and the HID monitor. At runtime:

- core 1 exclusively initializes, polls, and reconfigures UART controllers
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
core-1 RX polling and a hybrid core-1 TX path that fills the joined TX FIFO for short queues,
then lazily claims DMA when deeper backlog makes that path cheaper. Each ring has one producer and one
consumer: core 0 produces TX and consumes RX, while core 1 consumes TX and produces RX.

## Main Blocks

- USB device stack with 6 CDC ACM functions
- USB HID status-monitor function with LED-toggle and watchdog-reset commands
- Per-port CDC-to-UART routing in the USB poll loop
- Per-port RX and TX ring buffers inside each UART backend
- 2 hardware UART backends
- 4 PIO UART backends
- Board-specific GPIO configuration
- CDC DTR state opens the matching bridge; HID board controls are restricted
	to LED toggle and reset

## Design Notes

- Use TinyUSB for the multi-CDC USB device layer.
- Use Pico SDK for platform support.
- Keep pin mapping separate from bridge logic.
- Use separate RX and TX ring buffers per logical port.
- Use DMA for hardware UART RX and TX where the silicon already supports it well.
- Keep TinyUSB ownership in one execution context by polling it from `main`.
- Keep UART-controller ownership on core 1; cross-core traffic uses only the
	per-port rings and the control mailbox.
- Reserve RTS and CTS for hardware UART ports; enable them in the board configuration only when the connected peer supports flow control.
- PIO UART ports support 8N1; hardware UART ports additionally apply valid CDC data-bit,
	stop-bit, and parity settings.

## Open Items

- RTS and CTS runtime behavior for PIO UART ports
- Whether to replace PIO RX polling with IRQ or DMA service at sustained high baud rates
- Whether ring-overflow counters should be added to the compact HID report
