# Performance Test Plan

Start with the [Test Documentation Index](README.md) for test levels, result
semantics, and evidence requirements.

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

### Stage 1: CDC0 ↔ Debug Probe

Debug Probe UART is limited to standard baud rates. Test 115200 unless the
probe firmware supports custom baud rates.

### Stages 2, 3, 4: Link-internal UART pairs

For each stage, test these three layers at each candidate baud rate:

1. **Single direction** — `pair_duplex_benchmark.py --direction a-to-b` and
   `--direction b-to-a` separately, 10 s each.
2. **Full pair duplex** — `pair_duplex_benchmark.py --direction both`, 30 s.
3. **Concurrent six-port** — `serial_stress_benchmark.py` with all seven
   streams, 30 s per baud, independent process per baud, 8 s settle.

Record every tested rate even when it fails. A rate is stable only when
every direction and every stream passes.

| Layer | Rate sweep | Duration |
| --- | --- | --- |
| Single direction | 460800, 600000, 800000, 1000000, 1100000, 1200000 | 10 s per rate |
| Single pair duplex | 460800, 600000, 800000, 900000, 1000000, 1040000, 1060000, 1080000 | 30 s per rate |
| Concurrent six-port | 460800, 500000, 600000 | 30 s per rate, independent process |

### Baseline Run

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
python3 tools/hardware/serial_stress_benchmark.py \
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

Besides the `PASS`/`FAIL` lines run_performance_test.py parses, the benchmark
also prints one diagnostic `TIME <label>: {...}` line per stream with
thread-start/first-byte UTC and monotonic timestamps. These are for manually
diagnosing concurrent-startup skew and are not parsed by the runner.

To run only the benchmark and prepend a structured result entry automatically:

```sh
python3 tools/hardware/run_performance_test.py \
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
entry, use `tools/hardware/run_hardware_test.py`. It runs the functional stages
first and starts performance only when they pass unless
`--continue-after-functional-failure` is supplied.

When the full staged-fixture options are supplied, the benchmark uses the
documented cross-fixture: UART1 to UART2 and UART3 to UART4. This is the
required form for a full-matrix performance result.

The concurrent runner waits 8 seconds after each multi-port line-coding setup
before starting traffic so deferred PIO changes settle. Omitting UART1 or UART4
records a successful run as `PARTIAL`, not a full-matrix `PASS`. Pass
`--artifact /path/to/pico_uart.elf` or the flashed UF2 to bind the result to a
SHA-256 digest and HID-reported firmware version.

### Experimental arbitrary-baud and full six-port run

`serial_stress_benchmark.py` accepts any positive integer baud rate on Linux.
Standard rates use termios; other rates use Linux `termios2`/`BOTHER`. Use the
full staged mapping below for six CDC ports and twelve simultaneous UART
traffic directions:

```sh
PICO=/dev/serial/by-id/<pico-cdc-prefix>
PROBE=/dev/serial/by-id/<debug-probe-uart>

python3 tools/hardware/serial_stress_benchmark.py \\
  --uart0-pico "${PICO}-if00" --uart0-peer "$PROBE" \\
  --uart1 "${PICO}-if02" --uart1-peer "${PICO}-if04" \\
  --uart2 "${PICO}-if04" --uart3 "${PICO}-if06" \\
  --uart4 "${PICO}-if08" --uart4-peer "${PICO}-if06" \\
  --uart5 "${PICO}-if0a" --uart0-baud 115200 \\
  --rates 460800 --duration 30 --payload-bytes 1024 \\
  --timeout 3 --settle-seconds 8
```

The command exercises CDC0 in both directions through the Debug Probe, CDC1
and CDC2 as a cross-connected pair, CDC3 and CDC4 as a cross-connected pair,
and CDC5 through the physical loopback. It must be run as a separate process
for each candidate baud when comparing rates; do not change all CDC line
codings repeatedly inside one long-running process. First verify each pair
individually, then run the full six-port command. Treat a rate as stable only
when every stream passes in repeated runs. `--setup-only` can check that all
ports open and settle without transmitting, but it is not a performance pass.

For a single pair, use the synchronized diagnostic helper. It supports
`stage2` (CDC1↔CDC2) and `stage3` (CDC3↔CDC4), and can isolate one direction:

```sh
python3 tools/hardware/pair_duplex_benchmark.py stage2 \\
  --rates 460800,500000,600000 --runs 3 --duration 30 \\
  --settle 8 --direction both
python3 tools/hardware/pair_duplex_benchmark.py stage3 \\
  --rates 460800 --runs 3 --duration 30 --settle 8 --direction both
```

A `PASS` requires every payload to match. A setup or synchronization failure
must be recorded separately from a data failure; neither counts as a pass.

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