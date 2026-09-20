# Functional Test Plan

This plan verifies that each PicoUart USB CDC channel transfers data in both
directions and that the expected UART backend and wiring are operating.

## Scope

The functional test covers:

- USB enumeration of six CDC interfaces and one HID interface
- Bidirectional data transfer through the Debug Probe
- Hardware UART to PIO UART transfer
- PIO UART to PIO UART transfer
- PIO UART loopback
- Default UART configuration at 115200 baud, 8N1
- HID health status, RX errors, and RX overflow reporting

The staged wiring is defined in [Self-Test Setup](self-test-setup.md).

## Preconditions

1. Build and flash the intended firmware image.
2. Connect the PicoUart USB device to the host.
3. Confirm CDC0 through CDC5 and the HID interface enumerate.
4. Use stable paths under `/dev/serial/by-id` where available.
5. Confirm the selected test stage is wired exactly as documented.
6. Confirm RTS/CTS is disconnected unless the flow-control variant is being
   tested explicitly.

## Test Sequence

Run the stages in this order:

| Stage | Connection | Host endpoints | Expected result |
| --- | --- | --- | --- |
| 1 | Debug Probe to HW UART0 | CDC0 and Debug Probe UART | Bidirectional pass |
| 2 | HW UART1 to PIO UART2 | CDC1 and CDC2 | Bidirectional pass |
| 3 | PIO UART3 to PIO UART4 | CDC3 and CDC4 | Bidirectional pass |
| 4 | PIO UART5 loopback | CDC5 | Loopback pass |

For stages 1 through 3, run:

```sh
python3 tools/linux/serial_bridge_test.py \
  --pico-port /dev/serial/by-id/<pico-endpoint> \
  --peer-port /dev/serial/by-id/<peer-endpoint> \
  --label <stage-label>
```

For stage 4, run:

```sh
python3 tools/linux/serial_bridge_test.py \
  --pico-port /dev/serial/by-id/<pico-cdc5> \
  --loopback \
  --label stage4-pio-loopback
```

To run all four stages in order and record one newest-first result entry:

```sh
python3 tools/linux/run_functional_test.py \
  --pico-cdc0 /dev/serial/by-id/<pico-uart-cdc0> \
  --debug-probe /dev/serial/by-id/<debug-probe-uart> \
  --pico-cdc1 /dev/serial/by-id/<pico-uart-cdc1> \
  --pico-cdc2 /dev/serial/by-id/<pico-uart-cdc2> \
  --pico-cdc3 /dev/serial/by-id/<pico-uart-cdc3> \
  --pico-cdc4 /dev/serial/by-id/<pico-uart-cdc4> \
  --pico-cdc5 /dev/serial/by-id/<pico-uart-cdc5> \
  --board pico --firmware-version 1.2.3 --firmware-commit <commit>
```

For the complete functional-plus-performance sequence, use
`run_hardware_test.py` instead. It records one combined entry and controls
whether performance starts after the functional stages pass.

The runner stops at the first failed stage by default. Add
`--continue-on-failure` to collect every stage result. Add `--no-record` to
run without modifying the results file.

## Pass Criteria

The functional run passes only when:

- All six CDC endpoints enumerate.
- Stages 1 through 3 report both `PASS pico-to-peer` and
  `PASS peer-to-pico`.
- Stage 4 reports `PASS pico-loopback`.
- No unexpected board reset, USB disconnect, or serial framing error occurs.
- HID monitoring reports no new `rx_error` or `control_error` flags.
- The overflow count remains unchanged during the test.

When using `--all-baud-rates`, retain the default settle interval so deferred
CDC line-coding changes finish before traffic starts. Repeat any failed rate at
a fixed baud after the port has settled before treating it as a steady-state
link failure.

Record the result in [Performance Test Results](performance-test-results.md)
when the functional checks are run as part of a performance session. Record
standalone functional runs in the same file with a clear test type.

## Failure Handling

1. Stop after the first failed stage.
2. Record the exact command, wiring, firmware commit, and failure output.
3. Check TX/RX crossing, shared ground, baud configuration, and USB paths.
4. Re-run only the failed stage after correcting the setup.
5. Do not mark the overall test as passing when a stage was skipped; mark it
   `PARTIAL` and name the skipped stage.