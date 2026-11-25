# HIL Fixture Test Plan

This plan qualifies PicoUart on the fixed four-link fixture. Run the functional pass first, then the performance pass on
the same wiring. It is the baseline hardware test; releases also need the extra checks in the
[release HIL gates](../deployment/releasing.md#release-hil-gates).

Result meanings (`PASS`, `PARTIAL`, `NOT RUN`, and so on) and the evidence each record needs are defined in the
[test overview](README.md#results-and-evidence).

## Before You Start

Wire the fixed fixture and identify its endpoints using [HIL Fixture Setup](hil-fixture-setup.md). The manual Debug
Probe [external-peer check](hil-fixture-setup.md#optional-manual-external-peer-check) is optional and not part of HIL
pass criteria. Follow the [hardware authorization rules](README.md#hardware-authorization).

Test `pico` and `pico2` separately when available. Attach one PicoUart and confirm every CDC endpoint belongs to it:
the runners send UART data and change serial settings, but do not reset or flash the board. Flash and identify the
image using the [hardware setup guide](../hardware-setup.md#installing-firmware); `--artifact` hashes the supplied
file but does not identify the firmware running on the board. Use `tools/hil/.venv` for `pico-uart-hil` runners and
`host/python/.venv` for `pico-uart` host commands. Use `--no-record` when you do not want a report saved.

## Standard Combined Run

Use the combined runner for the baseline fixture test. It runs the functional pass first, then the concurrent six-port
performance pass if functional testing succeeds. It does not run the [pair sweeps](#pair-sweeps) or the
[recovery and stress procedures](hil-recovery-and-stress.md). Replace the example paths and metadata with the
identified board and the image actually flashed. The run writes one dated record; check its overall result and both
phase summaries.

```sh
tools/hil/runner/full.sh \
  --pico-cdc0 "/dev/serial/by-id/<pico-cdc0>" \
  --pico-cdc1 "/dev/serial/by-id/<pico-cdc1>" \
  --pico-cdc2 "/dev/serial/by-id/<pico-cdc2>" \
  --pico-cdc3 "/dev/serial/by-id/<pico-cdc3>" \
  --pico-cdc4 "/dev/serial/by-id/<pico-cdc4>" \
  --pico-cdc5 "/dev/serial/by-id/<pico-cdc5>" \
  --board pico --tester "<name>" \
  --firmware-version 1.2.3 --firmware-commit "<commit>" \
  --artifact /path/to/flashed/pico_uart.elf \
  --duration 30
```

Leave `--rates` unset to use the runner's default sweep, which is a [ceiling search](#baseline-and-speed-ceiling) above
115200; `--duration 30` follows the [test matrix](#test-matrix). `--skip-functional` and `--skip-performance` create a
`PARTIAL` record.

## Functional Pass

For a functional-only run or link diagnosis, use `tools/hil/runner/functional.sh` with the same `--pico-cdcN`,
board, firmware, and artifact arguments shown in the combined command. Use `--help` for the runner's full options.

The functional pass requires all of the following:

- CDC0-CDC5 and HID enumerate.
- All six channels are ready, with no initialization failure and no stuck pending request.
- Both crossed pairs pass in both directions, and both loopbacks pass.
- No unexpected reset, disconnect, serial framing error, increase in HID `rx_error_count`, or nonzero
  `last_control_error_reason`.
- RX overflow counts do not increase.

`--stage N` runs one fixture link for diagnosis; the result is `PARTIAL`. `--continue-on-failure` collects every link
result.

## Performance Pass

Every stream checks its returned payload byte for byte. Run the [pair sweeps](#pair-sweeps) for per-link
characterization or pair-specific claims.

### How the Runners Set Up Ports

The runners process the comma-separated rates sequentially in one process. Each rate gets a fresh setup:

1. Open every endpoint with DTR held low and apply the line coding.
2. Assert DTR for the group and wait for the HID readiness handshake. The handshake follows the
   [control plane's session rules](../design/control-plane-design.md#usb-mount-and-unmount-reset).
3. Wait the settle interval (8 s by default) and flush the host TTY queues, so no stale bytes leak into the next
   payload.
4. Take an HID health baseline, run traffic, take health again, then close the ports and restore the TTY settings.

If setup fails, nothing is transmitted. The setup reads back the host TTY speed and fails if it differs from the
requested rate by 50% or more. This checks the host driver setting; payload verification checks the link end to end.

### HID Health

Each rate needs clean HID health before and after traffic. Every channel must be closed or ready, with current
diagnostics, unchanged overrun counts, and no recorded RX or configuration faults. A health failure fails the rate even
if the payloads verified. Raw diagnostics, including buffer peaks, are kept in the record. A raw `stress` run adds the
same checks with `--check-hid-health`.

### Baseline and Speed Ceiling

The baseline is 115200 baud. A run passes only if all six streams pass at 115200 with clean HID health; if 115200
fails or was not run, higher-rate results cannot make it pass.

Above the baseline, an ascending sweep is a **ceiling search**: it stops at the first failed rate and reports later
rates as `NOT RUN`. Record the boundary separately from the baseline result:

- the highest rate where all six streams passed, and
- the first higher rate that failed, or that the cap was reached without a failure.

Untested rates in between are unknown, so do not claim an exact hardware limit. Any health failure fails the run. A
setup failure before the baseline also fails the run; one after a clean baseline leaves the baseline valid but the
ceiling undetermined.

For an evenly stepped search, add `--incremental-performance` to the combined command instead of `--rates`. It steps
up from 115200 in 100000-baud steps to 3000000 and stops at the first failure; adjust it with
`--incremental-start-rate`, `--incremental-rate-step`, and `--incremental-max-rate`. The standalone
`pico-uart-hil performance --incremental` takes the same options. A start above 115200 skips the baseline, so that run
cannot pass on its own. Every rate includes the settle time, so keep sweeps short and narrow the bracket with a smaller
step only where needed.

### Test Matrix

| Mode | Duration |
| --- | --- |
| Single direction, both directions on each crossed pair | 10 s per rate |
| Single pair duplex on each crossed pair | 30 s per rate |
| Concurrent six-port with fresh port setup | 30 s per rate |

Runner defaults differ, so pass `--duration` explicitly, as the examples do. Each runner command lists the rates it
tests.

### Pair Sweeps

Set `PICO_DEVICE_BASE` to the board's `/dev/serial/by-id` path without the `-ifNN` suffix; the runner adds the suffix
for each stage. The rates are selected characterization points, not uniform increments or a specification of the
hardware limit.

```sh
# Single direction, both directions on each crossed pair
for stage in stage1 stage2; do
  for direction in a-to-b b-to-a; do
    tools/hil/runner/pair.sh "$stage" --pico-device "$PICO_DEVICE_BASE" \
      --rates 460800,600000,800000,1000000,1100000,1200000 \
      --duration 10 --direction "$direction"
  done
done

# Duplex
for stage in stage1 stage2; do
  tools/hil/runner/pair.sh "$stage" --pico-device "$PICO_DEVICE_BASE" \
    --rates 460800,600000,800000,900000,1000000,1040000,1060000,1080000 \
    --duration 30 --direction both
done
```

Test each loopback with `tools/hil/runner/bridge.sh --pico-port "<endpoint>" --loopback`.

### Concurrent Six-Port

The [combined command](#standard-combined-run) is the default. For performance-only diagnosis, use
`tools/hil/runner/performance.sh` (see `--help`). It takes `--cdc0` through `--cdc5` instead of `--pico-cdc0` through
`--pico-cdc5` and uses the same default rate sweep.

## Timing Output

The benchmark prints one `TIME <label>: {...}` line per stream. It holds the host's thread-start, first-send-attempt,
and first-nonempty-read timestamps, in UTC and monotonic time. These are host-side times; use external instrumentation
to measure wire first-byte times. This diagnostic is not parsed by `pico-uart-hil performance`.

`--duration` stops new blocks from starting, but the last in-flight block can finish afterward. A write or read phase
that completes at or after its deadline counts as a timeout, even if the final bytes arrive right at the boundary.
Host scheduling jitter near the deadline can cause such a timeout.

## Short Diagnostic Run

For a quick post-change check, run the concurrent fixture for 60 seconds at 115200 baud, then test individual links at
921600 and 1000000. Capture HID health before and after, while no other monitor is reading the board:

```sh
host/python/.venv/bin/pico-uart monitor --duration 5
host/python/.venv/bin/pico-uart overruns
```

This is not release stability evidence; the [release HIL gates](../deployment/releasing.md#release-hil-gates) define
that. Earlier long runs are in the [evidence index](records/README.md#evidence-index).

## Known Gaps

Open hardware coverage:

- The fixture cannot test input that keeps arriving while the host has stopped reading. Every byte starts as a host
  write, so this needs an independently paced UART source.
- RTS/CTS has not been tested; there is no flow-control fixture yet.
- Pico 2 has much thinner coverage than Pico. Check the [evidence index](records/README.md#evidence-index) before
  relying on it; a Pico result does not carry over.
- Sustained DMA wrap/re-arm endurance with the normal transfer count remains untested. A shortened-countdown attempt
  produced invalid results; no safe injection procedure is validated yet.
- Watchdog recovery has one development observation, but no repeatable trigger procedure is documented. See the
  [review record](records/2026-10-23-uart-cdc-review-validation.md#configuration-isolation-and-recovery).
- Startup after each kind of reset remains unqualified. A startup failure right after SWD flashing turned out to be
  core 1 being launched without the reset the Pico SDK requires. The firmware now resets core 1 before launch
  ([architecture](../design/architecture.md#inter-core-coordination)); cold power-on, watchdog, and USB-reset starts
  are not qualified yet. Record procedures are in the
  [HIL recovery and stress plan](hil-recovery-and-stress.md#startup-observation).

For firmware parameter comparisons, follow the single
[tuning and measurement guide](../development/firmware-timing-and-buffer-measurements.md#comparing-tuning-candidates).
