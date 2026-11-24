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

Before running anything, note that these commands transmit UART data and change serial settings, so make sure the
endpoints belong to the fixture board. `--no-record` only suppresses the record; it is not a dry run.

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
- No unexpected reset, disconnect, serial framing error, new HID `rx_error`, or `control_error`.
- RX overflow counts do not increase.

`--stage N` runs one link for diagnosis; the result is `PARTIAL`. `--continue-on-failure` collects every stage result.

## Performance Pass

Every stream checks its returned payload byte for byte, so traffic alone is never a pass. Test each pair on its own,
then all six streams concurrently.

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
115200 fails or was not run, the baseline has not passed, whatever happened at higher rates.

Above the baseline, an ascending sweep is a **ceiling search**. It stops at the first failed rate and reports later
rates as `NOT RUN`. That first failure is the observed boundary, not a failed test. Record:

- the highest rate where all six streams passed, and
- the first higher rate that failed, or note that the cap was reached without a failure.

Do not claim an exact hardware limit; untested rates in between are unknown. A health or setup failure is always a
failure, whatever the baud rate.

### Test Matrix

| Layer               | Rate sweep                                                         | Duration                        |
| ------------------- | ------------------------------------------------------------------ | ------------------------------- |
| Single direction    | 460800, 600000, 800000, 1000000, 1100000, 1200000                  | 10 s per rate                   |
| Single pair duplex  | 460800, 600000, 800000, 900000, 1000000, 1040000, 1060000, 1080000 | 30 s per rate                   |
| Concurrent six-port | 115200, 128000, 153600, 230400, 256000, 460800, 921600, 1000000, 2000000, 3000000 | 30 s per rate, fresh port setup |

These durations are plan values, not runner defaults, so always pass `--duration`.

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

**Combined run.** The usual way to run the plan. `tools/hil/runner/full.sh` runs the functional pass, then the
concurrent performance pass if the functional pass succeeded, and writes a dated record to [records/](records/README.md).
It takes the functional pass's `--pico-cdcN`, `--board`, `--firmware-*`, and `--artifact` options, plus `--rates`,
`--duration`, `--timeout`, and `--performance-payload-bytes`.

`--skip-functional` and `--skip-performance` give a `PARTIAL` record even if the phase that ran succeeded. Check the
recorded scope, not just the exit code.

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

### Soak

Run the concurrent fixture for 60 seconds at 115200 baud, then test individual links at 921600 and 1000000. Capture
HID health before and after, while no other monitor is reading the board:

```sh
host/python/.venv/bin/python -m pico_uart.main monitor --duration 5
host/python/.venv/bin/python -m pico_uart.main overruns
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

| Case | Exercise | Acceptance and evidence |
| --- | --- | --- |
| All six channels loaded | Run the concurrent fixture and collect both cores' service load, block latency, ring peaks, and faults during traffic; use optional timing instrumentation for service gaps. | Every stream verifies payloads with no new loss/errors at the qualified load. Retain uninstrumented acceptance separately; host pacing is not proof of wire saturation. |
| Host drain stalled | With a bounded independently paced fixture source, pause host reads on one RX channel while the other channels continue; resume before the chosen capacity budget, then test a separate intentional-overflow interval. | Within budget, verify ordered bytes without loss. Beyond budget, require visible dropped-byte accounting and recovery, not lossless delivery; unaffected channels continue without new faults. |
| TX backpressure | Pause a fixture peer with supported CTS in a separately qualified flow-control variant while host OUT writes fill the channel; then resume the peer. | Host writes block or time out explicitly rather than silently losing bytes; admitted bytes remain ordered and other channels continue. Without a supported fixture variant, mark this case not run. |
| Close/reopen during traffic | Close one active channel while the other streams run, then reopen it with staged settings and require HID readiness before sending a fresh, distinguishable marker. | Do not require delivery of deliberately discarded queued bytes. No old-session payload may contaminate the fresh stream; applied settings match and unaffected channels keep passing. |
| Session loss with pending control | Keep a supported line-coding request pending using an authorized controlled busy condition, confirm pending through HID, then unplug or issue a targeted USB bus reset. | Old handles terminate or time out explicitly; after remount all old sessions are closed. Reopen, verify applied settings and a fresh marker, and check that canceled work produces no late timeout. Repeat with all six channels active. |
| Concurrent configuration | Repeatedly change supported settings on other channels while an independently paced channel receives verified traffic. Coordinate both ends of changed links. | Record maximum unaffected-port service gap and both cores' maximum pass times, not just average throughput. Require bounded completion or explicit timeout and no new faults on the unaffected stream. |

When changing line coding on a crossed pair, change both ends; a deliberate mismatch is not a bridge failure. A USB
reset affects the whole device, so the other streams are expected to stop.

Still open, and not closable with more USB loopback runs: independently paced input during USB stalls, CTS behavior,
RP2350 physical coverage, DMA re-arm endurance, and per-event startup reliability.

## Tuning Experiments

The current defaults (buffer sizes, bridge budgets, PIO DMA thresholds, TX admission gates, and DMA snapshot checks)
are backed by the [2026-10-23 review record](records/2026-10-23-uart-cdc-review-validation.md). Keep them unless an
experiment shows a clear, repeatable win. When you run one:

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

From the October 9 sweeps, using USB-paced traffic on the tested boards and images. This is historical evidence, not a
limit for later firmware.

| Image | Highest tested rate with all six payload streams passing | First failed tested rate | Evidence |
| --- | ---: | ---: | --- |
| Pico, 125 MHz | 256000 baud | 460800 baud | [Rated Pico](records/2026-10-09-070652Z-pico-hil.md) |
| Pico, 250 MHz | 921600 baud | 1000000 baud | [Overclock Pico](records/2026-10-09-071809Z-pico-250mhz-hil.md) |
| Pico 2, 150 MHz | 3000000 baud | None within sweep | [Rated Pico 2](records/2026-10-09-072834Z-pico2-hil.md) |
| Pico 2, 280 MHz | 3000000 baud | None within sweep | [Local 280 MHz build](records/2026-10-09-074700Z-pico2-280mhz-hil.md) |

Rates between the columns were not tested. The jump at 250 MHz suggests that service capacity in the dual-core path
limits a 125 MHz Pico, though no profiling was done and this is not a confirmed bottleneck.

USB is the shared ceiling on the faster boards. Each payload crosses USB twice (OUT to the source UART, then IN from
the destination), and each stream waits for its echo before sending more. Total verified throughput levels off around
0.47-0.53 MB/s, which points to USB full-speed limits. A pass at a given baud therefore shows integrity at the load
achieved; it says nothing about continuous full-rate traffic at that baud. Measuring that needs a different fixture:
independently paced external sources that bypass this board's USB OUT path, with wire timing measured.

## Results

The combined runner writes a dated record to [records/](records/README.md). Standalone runners append to the local,
ignored `build/hil-results.md`, which cannot qualify a release on its own. Keep transcripts of failures, and keep every
tested rate in the record, failures included.
