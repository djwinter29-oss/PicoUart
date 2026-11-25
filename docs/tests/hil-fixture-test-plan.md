# HIL Fixture Test Plan

This plan qualifies PicoUart on the fixed four-link fixture. Run the functional pass first, then the performance pass on
the same wiring. It is the baseline hardware test; releases also need the extra checks in the
[required HIL matrix](../deployment/releasing.md#required-hil-matrix).

Result meanings (`PASS`, `PARTIAL`, `NOT RUN`, and so on) and the evidence each record needs are defined in the
[test overview](README.md#results-and-evidence).

## Before You Start

Wire the fixed fixture and identify its endpoints using [HIL Fixture Setup](hil-fixture-setup.md). The manual Debug
Probe [external-peer check](hil-fixture-setup.md#optional-manual-external-peer-check) is optional and not part of HIL
pass criteria.

Test `pico` and `pico2` separately when available. Attach one PicoUart and confirm every CDC endpoint belongs to it:
the runners send UART data and change serial settings, but do not reset or flash the board. Flash and identify the
image using the [hardware setup guide](../hardware-setup.md#installing-firmware); `--artifact` hashes the supplied
file but does not identify the firmware running on the board. Use `--no-record` when you do not want a report saved.

## Standard Combined Run

Use the combined runner for the baseline fixture test. It runs the functional pass first, then the concurrent six-port
performance pass if functional testing succeeds. It does not run isolated pair sweeps or recovery/stress procedures.
Replace the example paths and metadata with the identified board and the image actually flashed. The run writes one
dated record; check its overall result and both phase summaries.

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
  --rates 115200,128000,153600,230400,256000,460800,921600,1000000,2000000,3000000 \
  --duration 30
```

The rates match the runner defaults; the 30-second duration matches this plan's concurrent test matrix. A later-rate
failure records the measured boundary, while the 115200-baud baseline remains a separate result.
`--skip-functional` and `--skip-performance` create a `PARTIAL` record even if the remaining phase passes.

## Functional Pass

Run this phase alone only when you need a functional-only result or are diagnosing a link:

```sh
tools/hil/runner/functional.sh \
  --pico-cdc0 "/dev/serial/by-id/<pico-cdc0>" \
  --pico-cdc1 "/dev/serial/by-id/<pico-cdc1>" \
  --pico-cdc2 "/dev/serial/by-id/<pico-cdc2>" \
  --pico-cdc3 "/dev/serial/by-id/<pico-cdc3>" \
  --pico-cdc4 "/dev/serial/by-id/<pico-cdc4>" \
  --pico-cdc5 "/dev/serial/by-id/<pico-cdc5>" \
  --board pico --firmware-version 1.2.3 --firmware-commit "<commit>" \
  --artifact /path/to/flashed/pico_uart.elf
```

The functional pass requires all of the following:

- CDC0-CDC5 and HID enumerate.
- All six channels are ready, with no initialization failure and no stuck pending request.
- Both crossed pairs pass in both directions, and both loopbacks pass.
- No unexpected reset, disconnect, serial framing error, increase in HID `rx_error_count`, or nonzero
  `last_control_error_reason`.
- RX overflow counts do not increase.

`--stage N` runs one link for diagnosis; the result is `PARTIAL`. `--continue-on-failure` collects every stage result.

## Performance Pass

Every stream checks its returned payload byte for byte. The combined run covers concurrent six-port traffic. The
isolated pair sweeps below are separate; run them for per-link characterization or when required by the
[release HIL matrix](../deployment/releasing.md#required-hil-matrix).

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

The baseline is 115200 baud. The run passes only if all six streams pass at 115200 with clean HID health. If
115200 fails or was not run, the baseline has not passed; higher-rate results do not change that.

Above the baseline, an ascending sweep is a **ceiling search**. It stops at the first failed rate and reports later
rates as `NOT RUN`. Record the first traffic failure as the measured boundary; the 115200-baud baseline remains a
separate result:

- the highest rate where all six streams passed, and
- the first higher rate that failed, or note that the cap was reached without a failure.

Do not claim an exact hardware limit; untested rates in between are unknown. Any health failure is a run failure. A
setup failure before the baseline also fails the run; one after a clean baseline leaves the baseline valid but the
ceiling undetermined.

### Test Matrix

| Mode | Duration |
| --- | --- |
| Single direction, both directions on each crossed pair | 10 s per rate |
| Single pair duplex on each crossed pair | 30 s per rate |
| Concurrent six-port with fresh port setup | 30 s per rate |

These are the durations used in this plan; examples pass them explicitly. Each runner command lists the rates it tests.

### Pair Sweeps

Set `PICO_DEVICE_BASE` to the board's `/dev/serial/by-id` path without the `-ifNN` suffix; the runner adds the suffix
for each stage.

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

The standalone runner uses `--cdcN`:

```sh
tools/hil/runner/performance.sh \
  --cdc0 "/dev/serial/by-id/<pico-cdc0>" \
  --cdc1 "/dev/serial/by-id/<pico-cdc1>" \
  --cdc2 "/dev/serial/by-id/<pico-cdc2>" \
  --cdc3 "/dev/serial/by-id/<pico-cdc3>" \
  --cdc4 "/dev/serial/by-id/<pico-cdc4>" \
  --cdc5 "/dev/serial/by-id/<pico-cdc5>" \
  --rates 115200,128000,153600,230400,256000,460800,921600,1000000,2000000,3000000 \
  --duration 30 --payload-bytes 1024 --timeout 3 \
  --board pico --firmware-version 1.2.3 --firmware-commit "<commit>" \
  --artifact /path/to/flashed/pico_uart.elf
```

### Ceiling Search

Replace `--rates` with `--incremental-performance` to step up from 115200 in 100000-baud steps, up to 3000000,
stopping at the first failure. Adjust it with `--incremental-start-rate`, `--incremental-rate-step`, and
`--incremental-max-rate`. A start above 115200 skips the baseline, so that run cannot pass on its own. The standalone
`pico-uart-hil performance --incremental` takes the same options. Narrow the bracket with a smaller step if needed.
Every rate includes the settle time, so keep sweeps short.

### Timing Output

The benchmark prints one `TIME <label>: {...}` line per stream. It holds the host's thread-start, first-send-attempt,
and first-nonempty-read timestamps, in UTC and monotonic time. These are host-side times; use external instrumentation
to measure wire first-byte times. This diagnostic is not parsed by `pico-uart-hil performance`.

`--duration` stops new blocks from starting, but the last in-flight block can finish afterward. A write or read phase
that completes at or after its deadline counts as a timeout, even if the final bytes arrive right at the boundary.
Host scheduling jitter near the deadline can cause such a timeout.

### Short Stability Check

Run the concurrent fixture for 60 seconds at 115200 baud, then test individual links at 921600 and 1000000. Capture
HID health before and after, while no other monitor is reading the board:

```sh
host/python/.venv/bin/pico-uart monitor --duration 5
host/python/.venv/bin/pico-uart overruns
```

Treat the 60-second run as a short check; include the tested duration in any stability claim. Earlier long runs are in
the [evidence index](records/README.md#evidence-index).

## Recovery and Stress Coverage

A normal performance run uses USB-paced traffic and does not exercise the cases below. Run them as separate, authorized
procedures on the exact flashed image.

<a id="repeatable-recovery-checks"></a>

### Automated Loopback Recovery

The `recovery` command uses the CDC1 and CDC5 loopbacks. Close all CDC sessions first. The command checks that both
endpoints belong to the given USB serial, and it never resets or flashes the board.

```sh
tools/hil/.venv/bin/pico-uart-hil recovery \
  --serial "<usb-serial>" --cdc1 "/dev/serial/by-id/<cdc1>" --cdc5 "/dev/serial/by-id/<cdc5>" \
  --artifact /path/to/flashed/pico_uart.elf --output build/recovery.json
```

The default `--mode all` runs four checks; `--mode churn|reopen|stall|overflow` runs one of them:

- configuration churn, with payloads checked on a sibling port
- five close/reopen cycles, each with a distinct payload
- a 2048-byte read stall of at least 250 ms
- a deliberate 16384-byte overflow

To pass, all payloads must verify, with no unexpected faults on any port. In the overflow check, only CDC1 may count
drops. The retained bytes plus the drops must equal the input, the retained bytes must stay in order, and fresh payloads
must pass after the backlog drains. The overflow leaves loss history behind, so reflash before any clean-health test.
The output file is never overwritten. Use an outer timeout for unattended runs.

<a id="startup-observations"></a>

### Startup Observation

Record each cold power-on, SWD reset, watchdog recovery, or USB reset as its own observation:

```sh
tools/hil/.venv/bin/pico-uart-hil startup \
  --serial "<usb-serial>" --event swd-reset --artifact /path/to/flashed/pico_uart.elf \
  --timeout 20 --output build/startup-swd-01.json
```

The command does not trigger the event; you do. For a reset, start it right after the reset; for power-on, start it
before applying power. `--event` is a label. The command passes when the board reports a valid clock and all six
channels stay closed for 12 seconds (`--stable-seconds`), with uptime advancing in step with host time.

For startup acceptance, add `--cdc1 "/dev/serial/by-id/<cdc1>"`. This transmits UART data: it opens CDC1 on its loopback,
waits for readiness, and verifies a 154-byte payload. Without it, the check only proves the board enumerates. Repeat
each event type, keep the failures as well as the passes, and never lengthen firmware sleeps to hide an unexplained
reset.

### Manual Cases

These cases need a fixture source or authorization beyond the runners. Record the board, artifact hash, rate, payload or
burst size, stall duration, affected channels, and HID samples. If a case cannot be set up, report it as not tested.

#### All six channels loaded

Run the concurrent fixture. Collect service load, block latency, ring peaks, and faults. Use timing instrumentation for
service gaps, then repeat acceptance on uninstrumented firmware. Every stream must verify without new loss or errors at
the qualified load. USB-paced traffic does not establish wire saturation.

#### Host drain stalled

Use a bounded, independently paced source. Pause reads on one RX channel while others continue; resume within the
capacity budget, then test a separate intentional-overflow interval. Within budget, bytes must arrive in order without
loss. Beyond budget, drops must be counted and traffic must recover, with no new faults on other channels.

#### TX backpressure

With a qualified flow-control fixture, pause a peer using supported CTS while host OUT writes fill the channel, then
resume it. Host writes must block or time out rather than lose bytes. Admitted bytes must stay ordered while other
channels continue.

#### Close and reopen during traffic

Close one channel while other streams run. Reopen with staged settings, wait for HID readiness, then send a fresh
marker. Old-session payloads must not enter the new stream; settings must match and other channels must keep passing.
Queued bytes discarded on close need not be delivered.

#### Session loss with pending control

Keep a supported line-coding request pending and confirm it through HID; unplug the board or issue a targeted USB bus
reset. Repeat with all six channels active. Old handles must terminate or time out. After remount, old sessions must be
closed, settings and a fresh marker must verify, and canceled work must not cause a late timeout. A USB reset stops
every stream.

#### Concurrent configuration

Repeatedly change supported settings on other channels while an independently paced channel carries verified traffic.
Change both ends of crossed links. Record the maximum unaffected-port service gap and both cores' maximum pass times.
Each change must complete or time out explicitly, with no new faults on unaffected traffic. A mismatched crossed pair
is not a bridge failure.

### Known Gaps

More USB loopback runs will not close these:

- The fixture cannot test input that keeps arriving while the host has stopped reading. Every byte starts as a host
  write, so this needs an independently paced UART source.
- RTS/CTS has not been tested; there is no flow-control fixture yet.
- Pico 2 has much thinner coverage than Pico. Check the [evidence index](records/README.md#evidence-index) before
  relying on it; a Pico result does not carry over.
- Sustained DMA wrap/re-arm endurance with the normal transfer count remains untested. A shortened-countdown attempt
  produced invalid results, and the safe injection procedure still needs validation.
- Startup after each kind of reset remains unqualified. A startup failure right after SWD flashing turned out to be
  core 1 being launched without the reset the Pico SDK requires; calling `multicore_reset_core1()` first fixed it in
  repeated trials ([review record](records/2026-10-23-uart-cdc-review-validation.md#explicit-core-1-reset-follow-up)).
  Cold power-on, watchdog, and USB-reset starts are not qualified yet.

## Tuning Procedure

Use current defaults as the baseline and compare candidates under matching conditions:

- Compare baseline and candidate images on both boards with the same clock, wiring, payloads, rates, and host. Change
  one setting per candidate, repeat the baseline to see run-to-run variation, and keep hashes and raw samples.
- Measure small-payload round trips as well as six-stream throughput, plus service gaps, ring peaks, fault deltas, and
  reopen behavior. The [timing profiles](../development/firmware-testing.md#optional-timing-profiles) and
  [buffer measurements](../development/firmware-testing.md#buffer-capacity-measurement) help here.
- As a proposed gate, require at least a 5% gain across five or more matched pairs, no more than 5% regression in p99 or
  maximum latency, no faults, and passing recovery checks. The benchmark reports only max and mean, so record
  distributions as well.
- Repeat payload acceptance on uninstrumented firmware. TX pipelining changes must keep CTS stalls, byte order, and the
  shifter-idle check before reconfiguration.
