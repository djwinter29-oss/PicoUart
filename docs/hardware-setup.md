# Hardware Setup

This document describes the PicoUart target-side UART pinout for Pico-class RP2040/RP2350 boards. It is for board wiring
and connector planning; the fixed hardware-in-the-loop test fixture is documented separately in
[HIL Fixture Setup](tests/hil-fixture-setup.md).

The [UART configuration](../firmware/src/config/uart_config.c) is the source of truth for this pinout.

## Port Map

The host sees six USB CDC ACM ports. Each CDC port maps to one target-side UART:

| USB CDC | UART  | Backend        | TX   | RX   | Optional RTS | Optional CTS |
| ------- | ----- | -------------- | ---- | ---- | ------------ | ------------ |
| CDC0    | UART0 | Hardware UART0 | GP0  | GP1  | GP3          | GP2          |
| CDC1    | UART1 | Hardware UART1 | GP4  | GP5  | GP7          | GP6          |
| CDC2    | UART2 | PIO UART       | GP8  | GP9  | GP10         | GP11         |
| CDC3    | UART3 | PIO UART       | GP12 | GP13 | GP14         | GP15         |
| CDC4    | UART4 | PIO UART       | GP16 | GP17 | GP18         | GP19         |
| CDC5    | UART5 | PIO UART       | GP20 | GP21 | GP22         | GP26         |

Normal UART wiring needs TX, RX, and a shared ground. RTS/CTS pins are reserved assignments, but they are not active in
the default firmware configuration.

## PicoUART6 Carrier Compatibility

With flow control disabled, PicoUart's six UART TX/RX pairs match the published pinout of
[8086net/pico-sexa-uart-bridge](https://github.com/8086net/pico-sexa-uart-bridge). This provides UART TX/RX pin
compatibility with the PicoUART6 carrier sold on [Lectronz](https://lectronz.com/products/picouart6) and
[The Pi Hut](https://thepihut.com/products/picouart6), subject to the following limits:

- The carrier's UART activity, I2C activity, and GPIO-controlled power LEDs are not driven by PicoUart. The Pico's own
	identification LED is separate and remains available on non-wireless Pico/Pico 2 boards.
- The STEMMA QT/Qwiic connector has no firmware I2C support: PicoUart does not expose a USB-to-I2C interface and is not
	compatible with the original firmware's `i2c-tiny-usb` functionality.
- Keep RTS/CTS disabled on this carrier. PicoUart's optional flow-control assignments overlap the carrier's LED and I2C
	wiring; enabling them requires a separately reviewed pin map and hardware setup.

This is a comparison of documented pin assignments, not a physical qualification of the carrier or a claim of full
feature compatibility with its original firmware.

## Default Firmware Behavior

RTS/CTS assignments are reserved but flow control is disabled by default. Enabling it is backend-specific; see the
[hardware UART](design/uart/hardware-uart-design.md) and [PIO UART](design/uart/pio-uart-design.md) designs. USB HID
reports status and board controls but does not change the pinout.

## Wiring Rules

- Connect PicoUart TX to the peer RX.
- Connect PicoUart RX to the peer TX.
- Share ground between PicoUart and every UART peer.
- Connect PicoUart RTS to peer CTS only when flow control is enabled and tested.
- Connect PicoUart CTS to peer RTS only when flow control is enabled and tested.
- Use 3.3 V UART logic. Do not connect RS-232 voltage-level signals directly to RP2040 or RP2350 GPIOs.

## GPIO Availability Notes

- This allocation targets Pico-class boards that expose GP0-GP22 and GP26-GP28 on headers.
- GP23 and GP24 are not used because they are not generally available on standard Pico headers.
- GP25 is reserved for the selected board's default LED when `PICO_DEFAULT_LED_PIN` is defined.
- GP26 is used as UART5 CTS when PIO flow control is enabled. In that configuration it is not available as an ADC input.
- GP27 and GP28 remain free for future use.

## Wireless Boards

Pico W and Pico 2 W expose the same UART pins listed above. Build specifically for `pico_w` or `pico2_w` using the
[firmware build guide](../firmware/build-and-config.md#build). The firmware operates as a USB/UART bridge only;
Wi-Fi and Bluetooth are not initialized or used.

The wireless boards' onboard LED is connected through the wireless chip rather than a direct MCU GPIO, so HID LED
identification is unavailable in these builds. Do not substitute the non-wireless `pico` or `pico2` image: it treats
GPIO25 as an LED output, but that pin is the wireless chip-select on W boards.

The release build/HIL matrix currently covers Pico and Pico 2, not the wireless variants. Successful compilation does
not qualify a Pico W or Pico 2 W for physical UART behavior, throughput, or clock-profile stability.

## Test Fixture

HIL fixture wiring belongs in [HIL Fixture Setup](tests/hil-fixture-setup.md). Use the
[HIL Fixture Test Plan](tests/hil-fixture-test-plan.md) for functional checks and performance benchmarks.

## Board Layout Guidance

- Group each UART channel as a repeated TX/RX/GND block.
- Put GND near every UART connector.
- Keep optional RTS/CTS adjacent to the matching TX/RX pair if the connector exposes flow control.
- Keep the board pin mapping isolated from USB and UART transport logic.
- Add test points for USB, power, and at least one hardware UART pair.

## Power Guidance

- USB normally powers the Pico board.
- Decide explicitly whether external UART targets are self-powered or powered from the fixture.
- Do not assume the Pico board can power six external targets directly.
- If a connector provides target power, document current limits and protection.