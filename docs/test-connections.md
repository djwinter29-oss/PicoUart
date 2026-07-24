# Test Connections

This setup verifies two PicoUart channels using a Raspberry Pi Debug Probe and a
Raspberry Pi serial console.

## UART0 To Debug Probe

PicoUart UART0 is exposed to the USB host as CDC0 and uses the hardware UART on
GP0 and GP1.

| PicoUart signal | Pico GPIO | Debug Probe UART signal |
| --- | --- | --- |
| TX | GP0 | RX |
| RX | GP1 | TX |
| GND | GND | GND |

Cross TX and RX. Do not connect a signal labelled TX to another TX signal.

## UART5 To Raspberry Pi

PicoUart UART5 is exposed to the USB host as CDC5 and uses the PIO UART on GP20
and GP21. The Raspberry Pi primary UART is available on GPIO14 (TXD) and GPIO15
(RXD).

| PicoUart signal | Pico GPIO | Raspberry Pi serial signal |
| --- | --- | --- |
| TX | GP20 | GPIO15 / RXD |
| RX | GP21 | GPIO14 / TXD |
| GND | GND | GND |

Use 3.3 V UART logic only. The Raspberry Pi UART header signals and Pico GPIOs
are not RS-232 voltage tolerant.

## Bring-Up Check

1. Flash PicoUart and connect its USB device port to the host.
2. Confirm that six CDC devices enumerate; CDC0 is the Debug Probe path and
   CDC5 is the Raspberry Pi path.
3. Configure both peer UARTs and the corresponding host CDC devices for the
   same baud rate. The firmware default is 115200 baud, 8N1.
4. Send known text through CDC0 and confirm it arrives at the Debug Probe UART.
5. Send known text through CDC5 and confirm it arrives at the Raspberry Pi
   serial console; repeat in the opposite direction for each channel.

The current firmware does not apply RTS or CTS. Test these links with TX, RX,
and GND only.