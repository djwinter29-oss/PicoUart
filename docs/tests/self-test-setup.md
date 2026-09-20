# Self-Test Setup

This procedure validates all six PicoUart channels in four staged wiring
configurations. Change the jumpers between stages; do not connect every stage
at the same time.

The USB host sees the channels as CDC0 through CDC5:

| USB CDC | UART | Backend | TX | RX |
| --- | --- | --- | --- | --- |
| CDC0 | UART0 | Hardware UART | GP0 | GP1 |
| CDC1 | UART1 | Hardware UART | GP4 | GP5 |
| CDC2 | UART2 | PIO UART | GP8 | GP9 |
| CDC3 | UART3 | PIO UART | GP12 | GP13 |
| CDC4 | UART4 | PIO UART | GP16 | GP17 |
| CDC5 | UART5 | PIO UART | GP20 | GP21 |

All connections use 3.3 V UART logic. Share ground between the Debug Probe and
the PicoUart board. The firmware defaults to 115200 baud, 8 data bits, no
parity, and 1 stop bit (8N1). RTS/CTS is disabled for this setup.

## RTS/CTS Flow Control

RTS/CTS is not active in the default firmware configuration. Do not connect
these pins for the normal self-test. When flow control is explicitly enabled,
cross the signals in the same way as TX and RX:

- PicoUart RTS connects to the peer CTS.
- PicoUart CTS connects to the peer RTS.

The board pin assignments are:

| UART | RTS | CTS |
| --- | --- | --- |
| UART0 | GP3 | GP2 |
| UART1 | GP7 | GP6 |
| UART2 | GP10 | GP11 |
| UART3 | GP14 | GP15 |
| UART4 | GP18 | GP19 |
| UART5 | GP22 | GP26 |

Before testing these lines, enable `hardware_flow_control` for the selected
hardware UART in `firmware/src/config/uart_board.c`. For PIO UARTs, enable both
`PIO_UART_DRIVER_PIN_FLAG_RX_FLOW_CONTROL` and
`PIO_UART_DRIVER_PIN_FLAG_TX_FLOW_CONTROL` as appropriate for the selected
port. Rebuild and flash the firmware after changing the configuration.

For example, the optional flow-control wiring for the two internal peer tests
is:

| Test | RTS connection | CTS connection |
| --- | --- | --- |
| UART1 to UART2 | GP7 (UART1 RTS) to GP11 (UART2 CTS) | GP6 (UART1 CTS) to GP10 (UART2 RTS) |
| UART3 to UART4 | GP14 (UART3 RTS) to GP19 (UART4 CTS) | GP15 (UART3 CTS) to GP18 (UART4 RTS) |

The peer must actively support RTS/CTS. A TX/RX data test passing does not
prove flow control is working; hold the peer CTS inactive during a transfer,
confirm that transmission pauses, then release it and verify that the complete
payload arrives in order. Remove RTS/CTS jumpers before returning to the
default firmware configuration.

## Equipment

- PicoUart board with a flashed Pico or Pico 2
- Raspberry Pi Debug Probe with its UART interface
- USB data cable from the PicoUart board to the host
- Jumper wires
- Host computer with the PicoUart host tools installed

The Debug Probe SWD connection is only needed for flashing and debugging. Its
UART connection is used as the external peer in stage 1.

## Stage 1: Debug Probe to Hardware UART

Use UART0/CDC0. Cross the signal directions:

| PicoUart UART0 | GPIO | Debug Probe UART |
| --- | --- | --- |
| TX | GP0 | RX |
| RX | GP1 | TX |
| GND | GND | GND |

Run the bidirectional bridge test:

```sh
python3 tools/serial_bridge_test.py \
  --pico-port /dev/serial/by-id/<pico-uart-cdc0> \
  --peer-port /dev/serial/by-id/<debug-probe-uart> \
  --label stage1-debug-probe-hw-uart
```

The test must report both `PASS pico-to-peer` and `PASS peer-to-pico`.

## Stage 2: Hardware UART to PIO UART

Remove the stage 1 signal jumpers. Connect hardware UART1/CDC1 to PIO
UART2/CDC2, crossing TX and RX:

| UART1 signal | GPIO | UART2 signal | GPIO |
| --- | --- | --- | --- |
| TX | GP4 | RX | GP9 |
| RX | GP5 | TX | GP8 |
| GND | GND | GND | GND |

Run:

```sh
python3 tools/serial_bridge_test.py \
  --pico-port /dev/serial/by-id/<pico-uart-cdc1> \
  --peer-port /dev/serial/by-id/<pico-uart-cdc2> \
  --label stage2-hw-to-pio
```

The test must report both `PASS pico-to-peer` and `PASS peer-to-pico`.

## Stage 3: PIO UART to PIO UART

Remove the stage 2 jumpers. Connect PIO UART3/CDC3 to PIO UART4/CDC4:

| UART3 signal | GPIO | UART4 signal | GPIO |
| --- | --- | --- | --- |
| TX | GP12 | RX | GP17 |
| RX | GP13 | TX | GP16 |
| GND | GND | GND | GND |

Run:

```sh
python3 tools/serial_bridge_test.py \
  --pico-port /dev/serial/by-id/<pico-uart-cdc3> \
  --peer-port /dev/serial/by-id/<pico-uart-cdc4> \
  --label stage3-pio-to-pio
```

The test must report both `PASS pico-to-peer` and `PASS peer-to-pico`.

## Stage 4: PIO UART Loopback

Remove the stage 3 jumpers. Loop back PIO UART5/CDC5 by connecting its TX to
its RX:

| PicoUart signal | GPIO | PicoUart signal | GPIO |
| --- | --- | --- | --- |
| TX | GP20 | RX | GP21 |

Run:

```sh
python3 tools/serial_bridge_test.py \
  --pico-port /dev/serial/by-id/<pico-uart-cdc5> \
  --loopback \
  --label stage4-pio-loopback
```

The test must report `PASS pico-loopback`.

## Bring-Up Checklist

1. Flash the firmware and connect the PicoUart USB device to the host.
2. Confirm that CDC0 through CDC5 enumerate. Prefer stable paths under
   `/dev/serial/by-id` instead of `/dev/ttyACM*`.
3. Complete stages 1 through 4 in order, moving the jumpers between stages.
4. Record the command output and firmware version for each stage.
5. Remove all test jumpers before connecting external UART equipment.

Never connect UART TX to TX or RX to RX. Do not connect RS-232 voltage-level
signals directly to Pico GPIOs. Stop the test and inspect the wiring if a
channel fails in both directions or if the board resets unexpectedly.

For flashing, USB enumeration, and extended stress testing, see
[`docs/test-connections.md`](../test-connections.md) and the board-testing
procedure in the repository skill documentation.