# Performance Test Plan

This plan measures sustained, bidirectional UART throughput and data integrity
across the PicoUart bridge. Run the functional test plan first.

## Hardware Requirement

Performance testing is user-run hardware-in-the-loop testing. CI does not
provide a physical Pico/Pico 2, Debug Probe, USB cable, or jumper fixture, so
the pipeline cannot execute these UART links or measure real throughput.
Assemble the fixture using [Self-Test Setup](self-test-setup.md), complete the
[Functional Test Plan](functional-test-plan.md), then run this plan locally.
Record measured results in [Performance Test Results](performance-test-results.md).

## Test Matrix

Test both supported board targets when hardware is available:

- Raspberry Pi Pico (`pico`, RP2040)
- Raspberry Pi Pico 2 (`pico2`, RP2350)

Test links:

| Link | USB endpoints | Wiring |
| --- | --- | --- |
| Debug Probe to HW UART0 | CDC0 and Debug Probe UART | Stage 1 |
| HW UART1 to PIO UART2 | CDC1 and CDC2 | Stage 2 |
| PIO UART3 to PIO UART4 | CDC3 and CDC4 | Stage 3 |
| PIO UART5 loopback | CDC5 | Stage 4 |

Use the fixture wiring in [Self-Test Setup](self-test-setup.md). The board GPIO
pinout is documented in [UART Pinout and Wiring](../uart-pinout.md). RTS/CTS is
excluded from the baseline performance test because it is disabled by default.
Test flow control separately after enabling and documenting it.

## Baseline Run

For each link, run an individual test at these rates:

```text
115200, 460800, 921600, 1000000 baud
```

Use a 10-second duration and the default payload size for the first pass. The
test must verify every returned byte, not only that traffic was transmitted.

After the individual runs pass, run all four links concurrently at 115200 baud
for 10 seconds. Full-speed USB bandwidth is the limiting resource for the
seven bidirectional streams; do not interpret a multi-link 460800+ failure as
a single-UART baud failure. Higher rates remain required as individual-link
tests and may be selected explicitly with `--rates` for experimental runs.

Example concurrent run using the existing benchmark tool:

```sh
python3 tools/serial_stress_benchmark.py \
  --uart0-pico /dev/serial/by-id/<pico-cdc0> \
  --uart0-peer /dev/serial/by-id/<debug-probe-uart> \
  --uart1 /dev/serial/by-id/<pico-cdc1> \
  --uart1-peer /dev/serial/by-id/<pico-cdc2> \
  --uart2 /dev/serial/by-id/<pico-cdc2> \
  --uart3 /dev/serial/by-id/<pico-cdc3> \
  --uart4 /dev/serial/by-id/<pico-cdc4> \
  --uart4-peer /dev/serial/by-id/<pico-cdc3> \
  --uart5 /dev/serial/by-id/<pico-cdc5> \
  --rates 115200 \
  --duration 10
```

To run only the benchmark and prepend a structured result entry automatically:

```sh
python3 tools/run_performance_test.py \
  --uart0-pico /dev/serial/by-id/<pico-uart-cdc0> \
  --uart0-peer /dev/serial/by-id/<debug-probe-uart> \
  --uart1 /dev/serial/by-id/<pico-cdc1> \
  --uart1-peer /dev/serial/by-id/<pico-cdc2> \
  --uart2 /dev/serial/by-id/<pico-uart-cdc2> \
  --uart3 /dev/serial/by-id/<pico-uart-cdc3> \
  --uart4 /dev/serial/by-id/<pico-cdc4> \
  --uart4-peer /dev/serial/by-id/<pico-cdc3> \
  --uart5 /dev/serial/by-id/<pico-uart-cdc5> \
  --board pico --firmware-version 1.2.3 --firmware-commit <commit>
```

Pass `--uart1`, `--uart1-peer`, `--uart4`, and `--uart4-peer` for the full
staged fixture. The runner preserves the benchmark exit code and records its
complete output. Use `--no-record` for a dry run.

For the complete functional-plus-performance sequence and one combined result
entry, use `tools/run_hardware_test.py`. It runs the functional stages
first and starts performance only when they pass unless
`--continue-after-functional-failure` is supplied.

When the full staged-fixture options are supplied, the benchmark uses the
documented cross-fixture: UART1 to UART2 and UART3 to UART4. This is the
required form for a full-matrix performance result.

The concurrent runner waits 2 seconds after each multi-port line-coding setup
before starting traffic so deferred PIO changes settle. Omitting UART1 or UART4
records a successful run as `PARTIAL`, not a full-matrix `PASS`. Pass
`--artifact /path/to/pico_uart.elf` or the flashed UF2 to bind the result to a
SHA-256 digest and HID-reported firmware version.

### Partial Bench Mode

`serial_stress_benchmark.py` still supports a smaller bench that omits UART1
and UART4. In that mode it uses the older UART2-to-UART3 cross-link plus the
UART5 loopback. Use this only for bring-up or diagnosis; record it as
`PARTIAL`, not as full HIL coverage.

## Soak Run

After the baseline passes, repeat the concurrent test for 60 seconds at
115200 baud. Run the individual-link soak at 921600 and 1000000 baud. Capture
HID status before and after each run:

```sh
python3 host/python/src/pico_uart_hid.py monitor --duration 5
python3 host/python/src/pico_uart_hid.py overruns
```

For a stress run, hold one CDC host endpoint closed briefly while peer traffic
continues. Record any expected ring-buffer overflow separately from unexpected
data corruption or UART errors.

## Measurements

Record the following for every run:

- Board and firmware version
- Firmware commit and build artifact name
- Host operating system and Python version
- Baud rate and duration
- Payload size and concurrent links
- Verified bytes and measured throughput per link
- RX errors, control errors, and RX overflow deltas
- USB disconnects, board resets, or serial exceptions
- Pass, fail, or partial result

## Acceptance Criteria

An individual run passes when all transmitted bytes are returned correctly in
both directions, the tool reports no failure, and the board remains connected.

A concurrent run passes when every included link meets the individual-run
criteria for the full duration. A performance run is `PARTIAL` when a link or
board was intentionally omitted; do not report it as a full-matrix pass.

Any data mismatch, unexplained byte loss, unexpected reset, persistent USB
disconnect, or new RX error is a failure even when the measured throughput is
high.

## Result Recording

Prepend each completed run to [Performance Test Results](performance-test-results.md).
Keep the newest result at the top. Include command output or a concise link to
raw logs when the output is too large for the results file.