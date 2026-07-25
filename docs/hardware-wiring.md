# Hardware Wiring

## Overview

PicoUart is intended to expose 6 USB CDC ports on the host side and 6 UART channels on the target side.
The current firmware architecture reserves:

- 2 hardware UART channels
- 4 PIO UART channels

This document describes the wiring model and the signals that each UART channel is expected to provide.

Hardware UART0 and UART1 reserve RTS and CTS for an optional runtime
flow-control configuration. PIO UART ports reserve the same signals for a
future flow-control implementation.

## Host Side

- USB device connection from the RP2040 or RP2350 board to the host PC
- The host sees 6 CDC ACM serial ports
- USB provides the control and data path for all 6 channels

## Target Side

Each UART channel should expose:

- TX
- RX
- RTS
- CTS
- GND

Hardware UART bring-up requires TX, RX, and GND. When its board configuration
enables flow control, also cross-connect RTS and CTS; the connected target must
honor RTS to avoid UART RX FIFO overflow during exceptional service gaps such
as a DMA re-arm. PIO UART bring-up currently requires TX, RX, and GND only.

## Channel Plan

| Channel | Backend | Required Signals | Notes |
| --- | --- | --- | --- |
| UART0 | Hardware UART | TX, RX, RTS, CTS, GND | Uses UART0 hardware pins |
| UART1 | Hardware UART | TX, RX, RTS, CTS, GND | Uses UART1 hardware pins |
| UART2 | PIO UART | TX, RX, RTS, CTS, GND | Uses PIO-managed GPIO block |
| UART3 | PIO UART | TX, RX, RTS, CTS, GND | Uses PIO-managed GPIO block |
| UART4 | PIO UART | TX, RX, RTS, CTS, GND | Uses PIO-managed GPIO block |
| UART5 | PIO UART | TX, RX, RTS, CTS, GND | Uses split GPIO block because GP23-GP25 are not header-accessible on Pico |

## Proposed Pico Pin Allocation

This allocation is intended for Pico-class boards that expose GP0-GP22 and GP26-GP28 on headers.

| Channel | Backend | TX | RX | RTS | CTS |
| --- | --- | --- | --- | --- | --- |
| UART0 | Hardware UART0 | GP0 | GP1 | GP3 | GP2 |
| UART1 | Hardware UART1 | GP4 | GP5 | GP7 | GP6 |
| UART2 | PIO UART | GP8 | GP9 | GP10 | GP11 |
| UART3 | PIO UART | GP12 | GP13 | GP14 | GP15 |
| UART4 | PIO UART | GP16 | GP17 | GP18 | GP19 |
| UART5 | PIO UART | GP20 | GP21 | GP22 | GP26 |

Notes:

- This uses 24 exposed GPIOs for 6 channels with RTS/CTS.
- GP27 and GP28 remain free for future use.
- GP23 and GP24 are not used because they are not generally available on standard Pico headers.
- GP25 is reserved for the selected board's default LED when `PICO_DEFAULT_LED_PIN` is defined.
- GP26 is used as a digital GPIO in this plan, so that ADC input is no longer available for other functions.

## Wiring Rules

- Connect PicoUart TX to target RX.
- Connect PicoUart RX to target TX.
- Connect PicoUart RTS to target CTS when hardware flow control is used.
- Connect PicoUart CTS to target RTS when hardware flow control is used.
- Share ground between PicoUart and the target device.
- Keep UART signal voltage levels compatible with the target system.
- Do not connect RS-232 level signals directly to RP2040 or RP2350 GPIOs.

## Bring-Up Loopbacks

The following temporary wiring validates the three PIO UART data paths without
an external UART peer:

| Test | Temporary wiring |
| --- | --- |
| UART2 to UART3 cross-connection | GP8 to GP13; GP12 to GP9 |
| UART5 loopback | GP20 to GP21 |

Remove this temporary wiring before attaching an external target. See
`docs/test-connections.md` for the matching host test commands.

## Power Notes

- USB powers the Pico board.
- Decide explicitly whether the target is self-powered or powered separately.
- Do not assume the Pico board should power 6 external targets directly.
- If target power output is needed, document current limits and protection clearly.

## Layout Notes

- Keep board pin mapping separate from firmware transport logic.
- Group each UART channel as a small repeated block to simplify routing.
- Reserve header space for GND near every UART signal group.
- Keep RTS and CTS adjacent to the matching TX and RX pins where possible.
- Consider test points for USB, power, and at least one hardware UART pair during bring-up.

## Open Items

- Connector style and pin order
- PIO UART RTS and CTS runtime implementation
- Whether one PCB supports both RP2040 and RP2350 variants or separate layouts are used