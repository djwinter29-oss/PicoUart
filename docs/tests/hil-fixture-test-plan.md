# HIL Fixture Test Plan

This plan qualifies PicoUart on the fixed four-link fixture. Run the functional pass first, then the performance pass on
the same wiring. It is the baseline hardware test; releases also need the extra checks in the
[required HIL matrix](../deployment/releasing.md#required-hil-matrix).

Result meanings (`PASS`, `PARTIAL`, `NOT RUN`, and so on) and the evidence each record needs are defined in the
[test overview](README.md#results-and-evidence).

## Before You Start

Wire the fixture as described in [HIL Fixture Setup](hil-fixture-setup.md):

| Stage | Connection             | CDC endpoints | Expected result    |
| ----- | ---------------------- | ------------- | ------------------ |
| 1     | HW UART0 ↔ PIO UART2  | CDC0, CDC2    | Bidirectional pass |
| 2     | PIO UART3 ↔ PIO UART4 | CDC3, CDC4    | Bidirectional pass |
| 3     | HW UART1 loopback      | CDC1          | Loopback pass      |
| 4     | PIO UART5 loopback     | CDC5          | Loopback pass      |

Test each board target (`pico` and `pico2`) when available, one board at a time. The Debug Probe external-peer check in
the setup guide is optional and not part of the pass criteria.

These commands send UART data and change serial settings. Confirm every endpoint belongs to the fixture board before
running them. `--no-record` only suppresses the report; the hardware test still runs.

Attach only one PicoUart. The runners find the board over HID rather than from the CDC paths, and `--board` is just a
record label. The six-port runners also check that every CDC path belongs to the same board, at its declared index.

Flash first, as described in [Releasing](../deployment/releasing.md#flashing-release-artifacts), then confirm the
version and ready state over HID. `--artifact` hashes the file you supply but cannot prove the board is running it, and
`--firmware-version` and `--firmware-commit` are labels too. The runners never reset, reflash, or change the board's
voltage, so after a failure reflash or reset yourself and confirm clean HID health before the next run.

## Functional Pass

```sh
tools/hil/runner/functional.sh \
  --pico-cdc0 /dev/serial/by-id/<pico-cdc0> \
  --pico-cdc1 /dev/serial/by-id/<pico-cdc1> \
  --pico-cdc2 /dev/serial/by-id/<pico-cdc2> \
  --pico-cdc3 /dev/serial/by-id/<pico-cdc3> \
  --pico-cdc4 /dev/serial/by-id/<pico-cdc4> \
  --pico-cdc5 /dev/serial/by-id/<pico-cdc5> \
  --board pico --firmware-version 1.2.3 --firmware-commit <commit> \
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

Every stream checks its returned payload byte for byte. Test each pair on its own, then all six streams concurrently.

### How the Runners Set Up Ports

The runners process the comma-separated rates sequentially in one process. Each rate gets a fresh setup:

1. Open every endpoint with DTR held low and apply the line coding.
2. Assert DTR for the group and wait for the HID readiness handshake. The handshake follows the
   [control plane's session rules](../design/control-plane-design.md#usb-mount-and-unmount-reset).
3. Wait the settle interval (8 s by default) and flush the host TTY queues, so no stale bytes leak into the next
   payload.
4. Take an HID health baseline, run traffic, take health again, then close the ports and restore the TTY settings.

If setup fails, nothing is transmitted. The setup also reads back the host TTY speed and fails if it differs from the
requested rate by 50% or more. That catches driver problems, but it is not a waveform measurement; payload verification
is the end-to-end check.

### HID Health

Each rate needs clean HID health before and after traffic. Every channel must be closed or ready, with current
diagnostics, unchanged overrun counts, and no recorded RX or configuration faults. A health failure fails the rate even
if the payloads verified. Raw diagnostics, including buffer peaks, are kept in the record. A raw `stress` run adds the
same checks with `--check-hid-health`.

### Baseline and Speed Ceiling

**The baseline is 115200 baud.** The run passes only if all six streams pass at 115200 with clean HID health. If
115200 fails or was not run, the baseline has not passed; higher-rate results do not change that.

Above the baseline, an ascending sweep is a **ceiling search**. It stops at the first failed rate and reports later
rates as `NOT RUN`. That first traffic failure is the observed boundary, not a failed test. Record:

- the highest rate where all six streams passed, and
- the first higher rate that failed, or note that the cap was reached without a failure.

Do not claim an exact hardware limit; untested rates in between are unknown. Any health failure is a run failure. A
setup failure before the baseline also fails the run; one after a clean baseline leaves the baseline valid but the
ceiling undetermined.

### Test Matrix

| Layer               | Rate sweep                                                         | Duration                        |
| ------------------- | ------------------------------------------------------------------ | ------------------------------- |
| Single direction    | 460800, 600000, 800000, 1000000, 1100000, 1200000                  | 10 s per rate                   |
| Single pair duplex  | 460800, 600000, 800000, 900000, 1000000, 1040000, 1060000, 1080000 | 30 s per rate                   |
| Concurrent six-port | 115200, 128000, 153600, 230400, 256000, 460800, 921600, 1000000, 2000000, 3000000 | 30 s per rate, fresh port setup |

These durations are plan values, not runner defaults, so always pass `--duration`.

**Combined run.** The usual way to run the plan. `tools/hil/runner/full.sh` runs the functional pass, then the
concurrent performance pass if the functional pass succeeded, and writes a dated record to
[records/](records/README.md). It takes the functional pass's `--pico-cdcN`, `--board`, `--firmware-*`, and
`--artifact` options, plus `--rates`, `--duration`, `--timeout`, and `--performance-payload-bytes`.
Keep failure transcripts and include every attempted rate in the record.

`--skip-functional` and `--skip-performance` give a `PARTIAL` record even if the phase that ran succeeded. Check the
recorded scope, not just the exit code.

**Pair sweeps.** Set `PICO_DEVICE_BASE` to the board's `/dev/serial/by-id` path without the `-ifNN` suffix; the runner
adds the suffix for each stage.

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

Test each loopback with `tools/hil/runner/bridge.sh --pico-port <endpoint> --loopback`.

**Concurrent six-port.** The standalone runner uses `--cdcN`:

```sh
tools/hil/runner/performance.sh \
  --cdc0 /dev/serial/by-id/<pico-cdc0> \
  --cdc1 /dev/serial/by-id/<pico-cdc1> \
  --cdc2 /dev/serial/by-id/<pico-cdc2> \
  --cdc3 /dev/serial/by-id/<pico-cdc3> \
  --cdc4 /dev/serial/by-id/<pico-cdc4> \
  --cdc5 /dev/serial/by-id/<pico-cdc5> \
  --rates 115200,128000,153600,230400,256000,460800,921600,1000000,2000000,3000000 \
  --duration 30 --payload-bytes 1024 --timeout 3 \
  --board pico --firmware-version 1.2.3 --firmware-commit <commit> \
  --artifact /path/to/flashed/pico_uart.elf
```

**Ceiling search.** Replace `--rates` with `--incremental-performance` to step up from 115200 in 100000-baud steps,
up to 3000000, stopping at the first failure. Adjust it with `--incremental-start-rate`, `--incremental-rate-step`, and
`--incremental-max-rate`. A start above 115200 skips the baseline, so that run cannot pass on its own. The standalone
`pico-uart-hil performance --incremental` takes the same options. Narrow the bracket with a smaller step if needed.
Every rate includes the settle time, so keep sweeps short.

### Timing Output

The benchmark prints one `TIME <label>: {...}` line per stream. It holds the host's thread-start, first-send-attempt,
and first-nonempty-read timestamps, in UTC and monotonic time. These are host times, not wire first-byte times, and they
are not parsed by `pico-uart-hil performance`.

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

Sixty seconds is a sanity check, not a stability claim; record a duration that matches what you want to claim. Earlier
long runs are in the [evidence index](records/README.md#evidence-index).

## Recovery and Stress Coverage

A normal performance run uses USB-paced traffic and does not exercise the cases below. Run them as separate, authorized
procedures on the exact flashed image.

<a id="repeatable-recovery-checks"></a>

### Automated Loopback Recovery

The `recovery` command uses the CDC1 and CDC5 loopbacks. Close all CDC sessions first. The command checks that both
endpoints belong to the given USB serial, and it never resets or flashes the board.

```sh
tools/hil/.venv/bin/pico-uart-hil recovery \
  --serial <usb-serial> --cdc1 /dev/serial/by-id/<cdc1> --cdc5 /dev/serial/by-id/<cdc5> \
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
  --serial <usb-serial> --event swd-reset --artifact /path/to/flashed/pico_uart.elf \
  --timeout 20 --output build/startup-swd-01.json
```

The command does not trigger the event; you do. For a reset, start it right after the reset; for power-on, start it
before applying power. `--event` is a label. The command passes when the board reports a valid clock and all six
channels stay closed for 12 seconds (`--stable-seconds`), with uptime advancing in step with host time.

For startup acceptance, add `--cdc1 /dev/serial/by-id/<cdc1>`. This transmits UART data: it opens CDC1 on its loopback,
waits for readiness, and verifies a 154-byte payload. Without it, the check only proves the board enumerates. Repeat
each event type, keep the failures as well as the passes, and never lengthen firmware sleeps to hide an unexplained
reset.

### Manual Cases

These cases need a fixture source or authorization beyond the runners. Record the board, artifact hash, rate, payload or
burst size, stall duration, affected channels, and HID samples. If a case cannot be set up, report it as not tested.

| Case | How to run it | Pass when |
| --- | --- | --- |
| All six channels loaded | Run the concurrent fixture; collect service load, block latency, ring peaks, and faults. Use timing instrumentation for service gaps, then repeat acceptance on uninstrumented firmware. | Every stream verifies with no new loss or errors at the qualified load. USB-paced traffic does not establish wire saturation. |
| Host drain stalled | Use a bounded, independently paced source. Pause reads on one RX channel while others continue; resume within the capacity budget, then test a separate intentional-overflow interval. | Within budget, bytes arrive in order without loss. Beyond budget, drops are counted and traffic recovers; other channels have no new faults. |
| TX backpressure | With a qualified flow-control fixture, pause a peer using supported CTS while host OUT writes fill the channel, then resume it. | Host writes block or time out rather than lose bytes. Admitted bytes stay ordered while other channels continue. |
| Close and reopen during traffic | Close one channel while other streams run. Reopen with staged settings, wait for HID readiness, then send a fresh marker. | Old-session payloads do not enter the new stream; settings match and other channels keep passing. Queued bytes discarded on close need not be delivered. |
| Session loss with pending control | Keep a supported line-coding request pending and confirm it through HID; unplug the board or issue a targeted USB bus reset. Repeat with all six channels active. | Old handles terminate or time out; after remount, old sessions are closed, settings and a fresh marker verify, and canceled work causes no late timeout. A USB reset stops every stream. |
| Concurrent configuration | Repeatedly change supported settings on other channels while an independently paced channel carries verified traffic. Change both ends of crossed links. | Record the maximum unaffected-port service gap and both cores' maximum pass times. Each change completes or times out explicitly, with no new faults on unaffected traffic. A mismatched crossed pair is not a bridge failure. |

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

## Tuning Experiments

The current defaults (buffer sizes, bridge budgets, PIO DMA thresholds, TX admission gates, and DMA snapshot checks)
are the baseline. The variants tried so far did not beat it:

- Halving the bridge pass budget (to 512 bytes) or the PIO TX DMA threshold (to 32 bytes) left throughput unchanged and
  made the worst block latency slightly higher ([tuning check](records/2026-10-22-cdc-uart-session-validation.md)).
- Dropping a TX admission check so the next transfer could start sooner ("refill") did not pay off. On the hardware
  UARTs it slowed small blocks at 115200. On PIO it gained about 5-8% for small blocks at 460800 but nothing at 115200,
  from one board and few samples. Both variants were removed; PIO refill may be worth another look for small-message
  workloads ([review record](records/2026-10-23-uart-cdc-review-validation.md#matched-refill-experiments)).

Keep the defaults unless an experiment shows a clear, repeatable win. When you run one:

- Compare baseline and candidate images on both boards with the same clock, wiring, payloads, rates, and host. Change
  one thing at a time, repeat the baseline to see run-to-run variation, and keep hashes and raw samples.
- Measure small-payload round trips as well as six-stream throughput, plus service gaps, ring peaks, fault deltas, and
  reopen behavior. The [timing profiles](../development/firmware-testing.md#optional-timing-profiles) and
  [buffer measurements](../development/firmware-testing.md#buffer-capacity-measurement) help here.
- As a proposed gate, require at least a 5% gain across five or more matched pairs, no more than 5% regression in p99 or
  maximum latency, no faults, and passing recovery checks. The benchmark reports only max and mean, so record
  distributions as well.
- Repeat payload acceptance on uninstrumented firmware. TX pipelining changes must keep CTS stalls, byte order, and the
  shifter-idle check before reconfiguration.

<a id="usb-paced-traffic-and-uart-capacity"></a>

## Measured Six-Port Envelope

The October 9 sweeps used USB-paced traffic and are historical evidence, not a limit for later firmware. Per-image
results are in the [HIL records](records/README.md#evidence-index). The clock matters on RP2040, since the 250 MHz Pico
passed far higher rates than the 125 MHz one, but no profiling was done to confirm the bottleneck.

On the faster boards, total verified throughput levels off around 0.47-0.53 MB/s. Each payload crosses USB twice, and
each stream waits for its echo, so this points to USB full-speed limits. A pass at a given baud therefore shows
integrity at the load achieved, not continuous full-rate traffic. Measuring that needs independently paced external
sources that bypass the board's USB OUT path, with wire timing measured.
