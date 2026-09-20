---
name: pico-uart-board-testing
description: "Use when: testing, bring-up, flashing, debugging, or validating a PicoUart board, including USB CDC enumeration, UART0 GP0/GP1 to a Raspberry Pi Debug Probe, UART1 GP4/GP5 loopback, UART2-to-UART3 cross-connection, UART4 GP16/GP17 loopback, UART5 GP20/GP21 loopback, concurrent serial stress benchmarking, and bidirectional UART traffic."
argument-hint: "Describe the board connection or test failure"
user-invocable: true
disable-model-invocation: false
---

# PicoUart Board Testing

Use this skill to validate a flashed PicoUart board on Linux. The supported
physical test links are:

- UART0: CDC0, GP0 TX and GP1 RX, connected to the Raspberry Pi Debug Probe UART
- UART1 loopback (optional): CDC1, GP4 TX jumpered to GP5 RX
- UART2 to UART3 cross-connection: CDC2 GP8 TX to CDC3 GP13 RX, and CDC3 GP12 TX to CDC2 GP9 RX
- UART4 loopback (optional): CDC4, GP16 TX jumpered to GP17 RX
- UART5 loopback: CDC5, GP20 TX jumpered to GP21 RX

The UART0 Debug Probe link requires crossed TX/RX wiring, a shared ground, and
3.3 V logic. The on-board test wiring uses only the listed Pico GPIOs.
UART0 is not part of a self-loopback; it is tested only against the external
Debug Probe UART. The common bench wiring keeps UART2↔UART3 and UART5 fitted,
while UART1 and UART4 loopbacks are optional.
Hardware UART0/UART1 leave RTS/CTS disabled by default in firmware. Cross-connect
RTS/CTS only when validating an explicit flow-control configuration against a
peer that drives CTS. PIO RX RTS is opt-in through its board pin flag; PIO CTS
TX gating is separately opt-in and pauses only before a new UART frame. Host
CDC RTS is ignored.

## Repository Test Documents

Use the repository documents as the source of truth for the staged self-test
and for recording physical test results:

- [`docs/tests/self-test-setup.md`](../../docs/tests/self-test-setup.md): pin
  assignments, staged wiring, and RTS/CTS connections.
- [`docs/tests/functional-test-plan.md`](../../docs/tests/functional-test-plan.md):
  functional test sequence and pass criteria.
- [`docs/tests/performance-test-plan.md`](../../docs/tests/performance-test-plan.md):
  performance matrix, soak runs, measurements, and acceptance criteria.
- [`docs/tests/performance-test-results.md`](../../docs/tests/performance-test-results.md):
  newest-first result log and test-result template.

The repository provides result-recording runners around the low-level serial
tools:

```sh
python3 tools/linux/run_functional_test.py --help
python3 tools/linux/run_performance_test.py --help
python3 tools/linux/run_hardware_test.py --help
```

Use `run_functional_test.py` for the four staged bridge/loopback checks. It
stops on the first failure by default, returns nonzero when the matrix does not
pass, and prepends the captured commands and output to the result log. Use
`--continue-on-failure` to collect all stages or `--no-record` for a dry run.
Use `run_performance_test.py` for the concurrent stress benchmark; it preserves
the benchmark exit code and records the measured bytes, throughput, and raw
output in the same newest-first log.
Use `run_hardware_test.py` for the normal end-to-end run. It starts the
functional matrix first, starts performance only after functional success by
default, and records one combined result entry. Add
`--continue-after-functional-failure` only when deliberately collecting
performance diagnostics after a failed functional stage.

When the requested test uses the full staged fixture, run the four stages in
the functional plan: Debug Probe to HW UART0, HW UART1 to PIO UART2, PIO UART3
to PIO UART4, and PIO UART5 loopback. The older individual loopback examples
below remain useful for partial bench wiring.

## Procedure

1. Source the SDK environment script, then build:

   ```sh
   . tools/linux/setup-sdk-env.sh
   tools/linux/build.sh
   ```

   This downloads Pico SDK `2.3.0` with submodules into the project-local,
   ignored `.pico-sdk` directory when it is not already present. To select a
   different Pico SDK release for this project:

   ```sh
   . tools/linux/setup-sdk-env.sh --sdk-version 2.3.0
   ```

2. Connect the Debug Probe SWDIO, SWCLK, and GND signals to the PicoUart board,
   then flash through CMSIS-DAP OpenOCD.

  Use a current OpenOCD build with CMSIS-DAP support. Some older or distro
  patched versions fail before programming with `cmsis-dap <cmd>` usage output
  or `Unknown flash device`; treat that as a tool/flash-support issue first.
  Flash ID `0x00154068` identifies a Boya BY25Q16ES 2 MiB device that is
  present in current upstream OpenOCD but may be missing from older distro
  packages.
  Check the version and retry at a lower SWD speed when needed:

  ```sh
  openocd --version
  tools/linux/load.sh --board pico --adapter-speed-khz 1000
  ```

   If the distro OpenOCD lacks the flash entry, install/build current upstream
   OpenOCD and select it explicitly:

   ```sh
   tools/linux/load.sh --openocd-exe /path/to/current/openocd \
     --board pico --adapter-speed-khz 1000
   ```

  The loader uses `interface/cmsis-dap.cfg` and does not need a
  `cmsis-dap vid_pid` command. Confirm the log reaches `SWD DPIDR`, target
  detection, and `Programming Started`. Record the OpenOCD version, flash ID,
  board, and adapter speed if `Unknown flash device` remains.

   **Bring-up / local rebuild** (default rebuilds then flashes):

   ```sh
   tools/linux/load.sh --board pico
   ```

   **Release HIL** must flash the **packaged** ELF whose SHA matches
   `SHA256SUMS-*` / the draft release — do not rebuild. Repeat the full matrix
   (steps 3–8) on both boards:

   ```sh
   tools/linux/load.sh --board pico --skip-build \
     --elf /path/to/pico_uart-vX.Y.Z-pico.elf
   # ... run steps 3–8, capture transcript + record SHA ...
   tools/linux/load.sh --board pico2 --skip-build \
     --elf /path/to/pico_uart-vX.Y.Z-pico2.elf
   # ... repeat steps 3–8 on the same wiring, capture transcript + record SHA ...
   ```

   This uses `target/rp2040.cfg` for `pico` and `target/rp2350.cfg` for
   `pico2`; no BOOTSEL button press or USB mass-storage mount is required.

3. Connect the flashed PicoUart device over USB and identify stable serial paths:

   ```sh
   ls -l /dev/serial/by-id
   lsusb
   ```

   Expect one PicoUart USB device with vendor/product ID `cafe:4010` and six
   CDC serial endpoints. Choose the endpoints corresponding to CDC0 through
   CDC5 for the cases below.
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

5. Verify the UART2-to-UART3 cross-connection and the fitted loopbacks:

   ```sh
   python3 tools/linux/serial_bridge_test.py \
     --pico-port /dev/serial/by-id/<pico-uart-cdc2> \
     --peer-port /dev/serial/by-id/<pico-uart-cdc3> \
     --label uart2-uart3-cross

   # Optional when the UART1 jumper is fitted.
   python3 tools/linux/serial_bridge_test.py \
     --pico-port /dev/serial/by-id/<pico-uart-cdc1> \
     --loopback --label uart1-gp4-gp5

   # Optional when the UART4 jumper is fitted.
   python3 tools/linux/serial_bridge_test.py \
     --pico-port /dev/serial/by-id/<pico-uart-cdc4> \
     --loopback --label uart4-gp16-gp17

   # Common bench loopback.
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
  benchmark. By default it keeps UART0 and the Debug Probe at 115200 baud,
  then sweeps UART2-to-UART3, UART5, and (when provided) UART1/UART4 across
  the supported rates while all configured streams run concurrently:

   ```sh
   python3 tools/linux/serial_stress_benchmark.py \
     --uart0-pico /dev/serial/by-id/<pico-uart-cdc0> \
     --uart0-peer /dev/serial/by-id/<debug-probe-uart> \
     --uart2 /dev/serial/by-id/<pico-uart-cdc2> \
     --uart3 /dev/serial/by-id/<pico-uart-cdc3> \
     --uart5 /dev/serial/by-id/<pico-uart-cdc5>
   # Optional when jumpers are fitted:
   #   --uart1 /dev/serial/by-id/<pico-uart-cdc1> \
   #   --uart4 /dev/serial/by-id/<pico-uart-cdc4>
   ```

  The default 10-second window reports verified bytes and measured throughput
  for every stream at 9600, 19200, 38400, 57600, 115200, 230400, 460800,
  921600, and 1000000 baud. Use `--rates`, `--duration`, and `--uart0-baud`
  for a focused longer run. `--uart1` / `--uart4` are optional; omit them when
  those jumpers are not fitted. Release promote does not require UART1/4 when
  the transcript documents that those jumpers were absent.

8. **HW UART RX stress / CDC-hold flood** (optional; not a substitute for the
   required bridge matrix). Hardware UART RX DMA re-arms via DMA IRQ when the
   countdown TRANS_COUNT exhausts (full 32-bit on RP2040; masked COUNT on
   RP2350 — see `firmware/src/uart/dma_progress.h`); the worker poll path is
   only a safety net. Default firmware leaves HW RTS/CTS **disabled** in
   `uart_board.c`. The flood below stresses ring/DMA backpressure with CDC
   held closed; it is **not** an RTS/CTS proof unless you first set
   `hardware_flow_control` true and wire RTS/CTS:

   ```sh
   # Terminal A: watch HID health bits (look for rx_overrun / rx_error).
   python3 host/python/src/pico_uart_hid.py monitor --duration 30

   # Terminal B: sustained peer→pico flood; defer opening pico CDC briefly so rings back up.
   python3 tools/linux/serial_bridge_test.py \
     --pico-port /dev/serial/by-id/<pico-uart-cdc0> \
     --peer-port /dev/serial/by-id/<peer-uart> \
     --baud 921600 --payload-bytes 4096 \
     --flood-seconds 20 --hold-cdc-seconds 5 \
     --label uart0-cdc-hold-flood
   ```

   Flood `PASS` only proves write/drain activity; pair with HID `monitor` for
   `rx_overrun` / `rx_error`. Capture command lines, baud, duration, HID lines,
   and `PASS`/`FAIL` output as a recorded HIL artifact for `docs/releasing.md`.

9. **PIO RTS/CTS validation** (required only when release notes claim PIO flow
  control). Enable both `PIO_UART_DRIVER_PIN_FLAG_RX_FLOW_CONTROL` and
  `PIO_UART_DRIVER_PIN_FLAG_TX_FLOW_CONTROL` for one PIO port in `uart_board.c`,
  then cross-connect its RTS to the peer CTS and its CTS to the peer RTS. Hold
  peer CTS high during a host-to-peer transfer, confirm no new PIO UART frame
  starts, release CTS, and verify the complete payload arrives in order. Repeat
  while holding the Pico CDC IN side to make Pico RTS deassert; verify the peer
  pauses without RX overrun. Record the wiring, CTS hold/release result, and
  `pico_uart_hid.py overruns` output.

## Diagnose Failures

- No `cafe:4010` device: verify the UF2 was flashed and use a USB data cable.
- OpenOCD cannot find the target: verify Debug Probe SWDIO, SWCLK, GND, and
   target power; the Debug Probe UART pins are not SWD pins.
- `cmsis-dap <cmd>` usage or `Unknown flash device`: update OpenOCD, verify the
  CMSIS-DAP config, reconnect target power, and retry with
  `--adapter-speed-khz 1000` before diagnosing firmware.
- Permission error: ensure the current user can access the serial device,
   typically through `dialout` or `plugdev` group membership.
- UART2-to-UART3 failure: confirm GP8-to-GP13 and GP12-to-GP9 are fitted.
- UART1 / UART4 / UART5 loopback failure: confirm the matching TX-to-RX jumper
  is fitted and no external target is connected to the same two pins.
- One failed direction: check that TX/RX are crossed and ground is shared.
- Both failed directions: verify matching baud rates and stop any serial-console
  service that is using `/dev/serial0`.
- Do not connect RS-232 voltage-level signals directly to PicoUart GPIOs.

For the complete wiring reference, see `docs/test-connections.md`.