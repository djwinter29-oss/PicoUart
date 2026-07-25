---
name: pico-uart-board-testing
description: "Use when: testing, bring-up, flashing, debugging, or validating a PicoUart board, including USB CDC enumeration, UART0 GP0/GP1 to a Raspberry Pi Debug Probe, UART2-to-UART3 cross-connection, UART5 GP20/GP21 loopback, concurrent serial stress benchmarking, and bidirectional UART traffic."
argument-hint: "Describe the board connection or test failure"
user-invocable: true
disable-model-invocation: false
---

# PicoUart Board Testing

Use this skill to validate a flashed PicoUart board on Linux. The supported
physical test links are:

- UART0: CDC0, GP0 TX and GP1 RX, connected to the Raspberry Pi Debug Probe UART
- UART2 to UART3 cross-connection: CDC2 GP8 TX to CDC3 GP13 RX, and CDC3 GP12 TX to CDC2 GP9 RX
- UART5 loopback: CDC5, GP20 TX jumpered to GP21 RX

The UART0 Debug Probe link requires crossed TX/RX wiring, a shared ground, and
3.3 V logic. The on-board PIO test wiring uses only the listed Pico GPIOs.
Hardware UART0/UART1 enable RTS/CTS in firmware (CTS is pulled down so TX still
flows when the peer omits CTS). Cross-connect RTS/CTS when validating flow
control against a peer that drives CTS. PIO UART RTS/CTS pins remain reserved
with no runtime flow-control behavior. Host CDC RTS is ignored.

## Procedure

1. Source the SDK environment script, then build:

   ```sh
   . tools/linux/setup-sdk-env.sh
   tools/linux/build.sh
   ```

   This downloads Pico SDK `2.2.0` with submodules into the project-local,
   ignored `.pico-sdk` directory when it is not already present. To select a
   different Pico SDK release for this project:

   ```sh
   . tools/linux/setup-sdk-env.sh --sdk-version 2.2.0
   ```

2. Connect the Debug Probe SWDIO, SWCLK, and GND signals to the PicoUart board,
   then flash the firmware through CMSIS-DAP OpenOCD:

   ```sh
   tools/linux/load.sh --board pico
   ```

   This uses `target/rp2040.cfg` for `pico` and `target/rp2350.cfg` for
   `pico2`; no BOOTSEL button press or USB mass-storage mount is required.

3. Connect the flashed PicoUart device over USB and identify stable serial paths:

   ```sh
   ls -l /dev/serial/by-id
   lsusb
   ```

   Expect one PicoUart USB device with vendor/product ID `cafe:4010` and six
   CDC serial endpoints. Choose the endpoints corresponding to CDC0, CDC2,
   CDC3, and CDC5.
   Do not assume `/dev/ttyACM` numbering is stable.

4. Verify UART0 and the Debug Probe. The Debug Probe UART usually appears as a
   `/dev/serial/by-id/usb-Raspberry_Pi_Debugprobe_on_Pico__CMSIS-DAP__*-if01`
   path:

   ```sh
   python3 tools/linux/serial_bridge_test.py \
     --pico-port /dev/serial/by-id/<pico-uart-cdc0> \
     --peer-port /dev/serial/by-id/<debug-probe-uart> \
     --label uart0-debug-probe
   ```

5. Verify the UART2-to-UART3 cross-connection and UART5 loopback:

   ```sh
   python3 tools/linux/serial_bridge_test.py \
     --pico-port /dev/serial/by-id/<pico-uart-cdc2> \
     --peer-port /dev/serial/by-id/<pico-uart-cdc3> \
     --label uart2-uart3-cross
   python3 tools/linux/serial_bridge_test.py \
     --pico-port /dev/serial/by-id/<pico-uart-cdc5> \
     --loopback --label uart5-gp20-gp21
   ```

6. UART0 passes when it prints both `PASS pico-to-peer` and `PASS peer-to-pico`.
   The UART2-to-UART3 cross-connection uses the same two pass markers. A
   loopback passes when it prints `PASS pico-loopback`. The default is 115200
   baud, 8N1. Use `--baud`, `--payload-bytes`, and `--timeout` for supported
   alternate baud rates, longer transfers, or slower peers.

7. After the 115200 baud smoke tests pass, run the concurrent performance
   benchmark. It keeps UART0 and the Debug Probe at 115200 baud, then sweeps
   UART2-to-UART3 and UART5 across the supported standard PIO rates while all
   five data streams run concurrently:

   ```sh
   python3 tools/linux/serial_stress_benchmark.py \
     --uart0-pico /dev/serial/by-id/<pico-uart-cdc0> \
     --uart0-peer /dev/serial/by-id/<debug-probe-uart> \
     --uart2 /dev/serial/by-id/<pico-uart-cdc2> \
     --uart3 /dev/serial/by-id/<pico-uart-cdc3> \
     --uart5 /dev/serial/by-id/<pico-uart-cdc5>
   ```

   The default 10-second window reports verified bytes and measured throughput
   for every stream at 9600, 19200, 38400, 57600, 115200, 230400, 460800,
   921600, and 1000000 baud. Use `--rates` and `--duration` for a focused
   longer run.

8. **HW UART RX stress with an RTS-ignoring peer** (release-candidate / HIL
   recording). Hardware UART RX DMA re-arms via DMA IRQ when the 32-bit transfer
   counter exhausts; the worker poll path is only a safety net. With RTS/CTS
   enabled, a well-behaved peer should not overrun. Example skeleton when the
   peer ignores RTS (leave CTS unconnected / peer does not wire RTS):

   ```sh
   # Terminal A: watch HID health bits (look for rx_overrun / rx_error).
   python3 host/python/src/pico_uart_hid.py monitor --duration 30

   # Terminal B: flood UART0 from an RTS-ignoring peer while CDC0 drains slowly
   # or is held closed for part of the window, then reopen and check integrity.
   python3 tools/linux/serial_bridge_test.py \
     --pico-port /dev/serial/by-id/<pico-uart-cdc0> \
     --peer-port /dev/serial/by-id/<rts-ignoring-peer> \
     --baud 921600 --payload-bytes 65536 --label uart0-rts-ignore-flood
   ```

   Capture command lines, baud, duration, HID `monitor` lines, and `PASS`/`FAIL`
   output as a recorded HIL artifact for `docs/releasing.md`.

## Diagnose Failures

- No `cafe:4010` device: verify the UF2 was flashed and use a USB data cable.
- OpenOCD cannot find the target: verify Debug Probe SWDIO, SWCLK, GND, and
   target power; the Debug Probe UART pins are not SWD pins.
- Permission error: ensure the current user can access the serial device,
   typically through `dialout` or `plugdev` group membership.
- UART2-to-UART3 failure: confirm GP8-to-GP13 and GP12-to-GP9 are fitted.
- Loopback failure: confirm the GP20-to-GP21 jumper is fitted and no external
   target is connected to the same two pins.
- One failed direction: check that TX/RX are crossed and ground is shared.
- Both failed directions: verify matching baud rates and stop any serial-console
  service that is using `/dev/serial0`.
- Do not connect RS-232 voltage-level signals directly to PicoUart GPIOs.

For the complete wiring reference, see `docs/test-connections.md`.