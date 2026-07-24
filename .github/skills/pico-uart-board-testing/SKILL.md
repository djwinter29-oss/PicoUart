---
name: pico-uart-board-testing
description: "Use when: testing, bring-up, flashing, debugging, or validating a PicoUart board, including USB CDC enumeration, UART0 GP0/GP1 to a Raspberry Pi Debug Probe, UART5 GP20/GP21 to Raspberry Pi GPIO14/GPIO15, serial throughput, and bidirectional UART traffic."
argument-hint: "Describe the board connection or test failure"
user-invocable: true
disable-model-invocation: false
---

# PicoUart Board Testing

Use this skill to validate a flashed PicoUart board on Linux. The supported
physical test links are:

- UART0: CDC0, GP0 TX and GP1 RX, connected to the Raspberry Pi Debug Probe UART
- UART5: CDC5, GP20 TX and GP21 RX, connected to Raspberry Pi GPIO15 RXD and GPIO14 TXD

Both links require crossed TX/RX wiring, a shared ground, and 3.3 V logic.
RTS and CTS are not implemented by the current firmware.

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
   CDC serial endpoints. Choose the endpoints corresponding to CDC0 and CDC5.
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

5. Verify UART5 and the Raspberry Pi primary UART:

   ```sh
   python3 tools/linux/serial_bridge_test.py \
     --pico-port /dev/serial/by-id/<pico-uart-cdc5> \
     --peer-port /dev/serial0 \
     --label uart5-raspberry-pi
   ```

6. A passing link prints both `PASS pico-to-peer` and `PASS peer-to-pico`. The
   default is 115200 baud, 8N1. Use `--baud`, `--payload-bytes`, and `--timeout`
   for supported alternate baud rates, longer transfers, or slower peers.

## Diagnose Failures

- No `cafe:4010` device: verify the UF2 was flashed and use a USB data cable.
- OpenOCD cannot find the target: verify Debug Probe SWDIO, SWCLK, GND, and
   target power; the Debug Probe UART pins are not SWD pins.
- Permission error: ensure the current user can access the serial device,
  typically through `dialout` or `plugdev` group membership.
- One failed direction: check that TX/RX are crossed and ground is shared.
- Both failed directions: verify matching baud rates and stop any serial-console
  service that is using `/dev/serial0`.
- Do not connect RS-232 voltage-level signals directly to PicoUart GPIOs.

For the complete wiring reference, see `docs/test-connections.md`.