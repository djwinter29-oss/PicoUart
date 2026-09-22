# UART Pinout and Wiring

This document describes the PicoUart target-side UART pinout for Pico-class
RP2040/RP2350 boards. It is for board wiring and connector planning; the fixed
hardware-in-the-loop test fixture is documented separately in
[Self-Test Setup](tests/self-test-setup.md).

The firmware source of truth for this pinout is
`firmware/src/board/uart_board.c`.

## Port Map

The host sees six USB CDC ACM ports. Each CDC port maps to one target-side UART:

| USB CDC | UART | Backend | TX | RX | Optional RTS | Optional CTS |
| --- | --- | --- | --- | --- | --- | --- |
| CDC0 | UART0 | Hardware UART0 | GP0 | GP1 | GP3 | GP2 |
| CDC1 | UART1 | Hardware UART1 | GP4 | GP5 | GP7 | GP6 |
| CDC2 | UART2 | PIO UART | GP8 | GP9 | GP10 | GP11 |
| CDC3 | UART3 | PIO UART | GP12 | GP13 | GP14 | GP15 |
| CDC4 | UART4 | PIO UART | GP16 | GP17 | GP18 | GP19 |
| CDC5 | UART5 | PIO UART | GP20 | GP21 | GP22 | GP26 |

Normal UART wiring needs TX, RX, and a shared ground. RTS/CTS pins are reserved
assignments, but they are not active in the default firmware configuration.

## Default Firmware Behavior

- Hardware UART0 and UART1 leave RTS/CTS disabled by default
  (`hardware_flow_control = false`). Their RTS/CTS pins are not muxed unless
  flow control is explicitly enabled in `firmware/src/board/uart_board.c`.
- PIO UART RTS/CTS pins are assigned but not claimed by default. PIO RX RTS is
  claimed only when `PIO_UART_DRIVER_PIN_FLAG_RX_FLOW_CONTROL` is enabled. CTS
  TX gating is claimed only when `PIO_UART_DRIVER_PIN_FLAG_TX_FLOW_CONTROL` is
  enabled.
- The USB HID interface reports status and board controls, but it does not
  change this static pinout.

## Wiring Rules

- Connect PicoUart TX to the peer RX.
- Connect PicoUart RX to the peer TX.
- Share ground between PicoUart and every UART peer.
- Connect PicoUart RTS to peer CTS only when flow control is enabled and tested.
- Connect PicoUart CTS to peer RTS only when flow control is enabled and tested.
- Use 3.3 V UART logic. Do not connect RS-232 voltage-level signals directly to
  RP2040 or RP2350 GPIOs.

## GPIO Availability Notes

- This allocation targets Pico-class boards that expose GP0-GP22 and GP26-GP28
  on headers.
- GP23 and GP24 are not used because they are not generally available on
  standard Pico headers.
- GP25 is reserved for the selected board's default LED when
  `PICO_DEFAULT_LED_PIN` is defined.
- GP26 is used as UART5 CTS when PIO flow control is enabled. In that
  configuration it is not available as an ADC input.
- GP27 and GP28 remain free for future use.

## Test Fixture

HIL fixture wiring belongs in [Self-Test Setup](tests/self-test-setup.md).
Use [Functional Test Plan](tests/functional-test-plan.md) for staged bridge
checks and [Performance Test Plan](tests/performance-test-plan.md) for
concurrent benchmarks.

## Board Layout Guidance

- Group each UART channel as a repeated TX/RX/GND block.
- Put GND near every UART connector.
- Keep optional RTS/CTS adjacent to the matching TX/RX pair if the connector
  exposes flow control.
- Keep the board pin mapping isolated from USB and UART transport logic.
- Add test points for USB, power, and at least one hardware UART pair.

## Power Guidance

- USB normally powers the Pico board.
- Decide explicitly whether external UART targets are self-powered or powered
  from the fixture.
- Do not assume the Pico board can power six external targets directly.
- If a connector provides target power, document current limits and protection.

## Future Board Decisions

The firmware pinout above is defined. A PCB or harness that uses it still needs
separate mechanical choices:

- Connector style and pin order
- Whether the six UART ports are exposed as one header block or repeated
  per-port connectors