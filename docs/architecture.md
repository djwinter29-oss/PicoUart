# Architecture

## Overview

PicoUart is a USB-to-UART bridge for RP2040 and RP2350.
The device presents 6 USB CDC interfaces to the host.
The device also presents 1 USB HID interface for status monitoring.
Each CDC interface maps to one UART channel.

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
-> per-port bridge logic
-> UART TX/RX backend
-> target device

Each port should work independently so traffic on one UART does not block the others more than necessary.

## Main Blocks

- USB device stack with 6 CDC ACM functions
- USB HID status-monitor function
- Per-port buffering and routing
- 2 hardware UART backends
- 4 PIO UART backends
- Board-specific GPIO configuration
- Per-port RTS/CTS flow-control handling

## Design Notes

- Use TinyUSB for the multi-CDC USB device layer.
- Use Pico SDK for platform support.
- Keep pin mapping separate from bridge logic.
- Plan for RTS and CTS on all 6 UART channels.
- Start with one working port, then scale to all 6.

## Open Items

- Supported baud-rate range per port
- Exact RTS/CTS implementation details for the 4 PIO-backed channels
- DMA and interrupt strategy