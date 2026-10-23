# HIL Fixture Test Plan

Use this plan to qualify the fixed PicoUart HIL fixture. It covers functional validation first, then performance testing
on the same wiring. Physical hardware is required; CI runs host tests and firmware builds only.

This is the baseline fixture plan, not the complete release gate. Release qualification also requires the recovery and
stress checks listed in [Releasing](../deployment/releasing.md#required-hil-matrix).

## Setup

Follow [HIL Fixture Setup](hil-fixture-setup.md) and install both crossed pairs and both loopbacks before testing. The
[Hardware Setup](../hardware-setup.md) document is the GPIO source of truth. Use the manual Debug Probe
external-peer check only when needed; it is not part of HIL pass criteria.

Test both board targets when hardware is available, one board at a time:

- Raspberry Pi Pico (`pico`, RP2040)
- Raspberry Pi Pico 2 (`pico2`, RP2350)

| Stage | Connection             | CDC endpoints | Expected result    |
| ----- | ---------------------- | ------------- | ------------------ |
| 1     | HW UART0 ↔ PIO UART2  | CDC0, CDC2    | Bidirectional pass |
| 2     | PIO UART3 ↔ PIO UART4 | CDC3, CDC4    | Bidirectional pass |
| 3     | HW UART1 loopback      | CDC1          | Loopback pass      |
| 4     | PIO UART5 loopback     | CDC5          | Loopback pass      |

All links use 3.3 V UART signaling, 8N1 at the default 115200 baud, and shared ground. RTS/CTS is disconnected unless an
explicit flow-control variant is under test.

The commands below change serial settings and transmit UART data. Identify the intended board and endpoints before
running them, and use the dedicated fixture rather than ports connected to unrelated equipment. `--help` displays
usage; `--no-record` suppresses result recording but still runs hardware tests and is not a dry-run mode.

The runners' HID health queries use automatic discovery, not the supplied CDC paths. Keep only the intended PicoUart
board available to those queries and verify that all six CDC paths belong to it. `--board` is a record label, not a
device selector; attaching a second compatible board can make HID discovery ambiguous and invalidate health evidence.

Flash the intended image using the [release flashing procedure](../deployment/releasing.md#flashing-release-artifacts)
before testing, then verify its HID firmware version and ready state. `--artifact` hashes the supplied file; it does not
read back flash or prove the board is running that file. Version and commit arguments are record labels, so match them
to the flashed artifact and retain the flashing evidence rather than relying on matching labels alone.

## Functional Pass

Run the functional stages in order with all four links installed. The runner tests pairs in both directions and each
self-loopback on its own CDC endpoint:

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

Functional pass criteria:

- CDC0 through CDC5 and HID enumerate.
- All six UART channels are ready, with no initialization failure or unresolved control-pending state.
- Both crossed pairs pass in both directions; both loopbacks pass.
- No unexpected reset, disconnect, serial framing error, new HID `rx_error`, or `control_error` occurs.
- RX overflow counts do not increase.

Use `--stage 1`, `--stage 2`, `--stage 3`, or `--stage 4` to diagnose one fixture link. A single-stage run is `PARTIAL`,
not a full functional pass. Use `--continue-on-failure` to collect all stage results and `--no-record` only to suppress
the result log.

## Performance Pass

Run performance testing only after functional pass. Every stream verifies returned payload bytes; traffic activity alone
is not a pass. Test each pair individually, then run all six streams concurrently. The pair and concurrent runners
process comma-separated rates sequentially in one process; they reopen and reconfigure the ports and wait for the settle
interval before testing each rate. After settling, the runner flushes all host TTY queues so bytes arriving during
CDC re-enumeration or DTR-triggered channel reset cannot contaminate the next rate's first payload.

For each paired functional test, the bridge runner opens both endpoints before applying either line coding. For each
concurrent rate, the stress runner opens all six CDC endpoints first. Identified PicoUart endpoints hold DTR low during
configuration. Setup applies line coding without fixed setup sleeps, then asserts DTR for the configured group and
requires a bounded HID readiness handshake before returning descriptors or transmitting. Linux sysfs associates each
selected TTY with its USB serial number and CDC interface. The six-port runner requires every endpoint to match its
declared CDC index on the same board; unidentified, swapped, duplicate, or mixed-board endpoints fail setup before
group DTR assertion. Paired bridge tests still permit a generic external UART peer. HID auto-discovery of an unrelated
board is not used for readiness.
The handshake contract and timeout are defined in [Control Plane Design](../design/control-plane-design.md#usb-mount-and-unmount-reset).
Failure closes all opened endpoints and prevents payload transmission. After traffic, closing deinitializes each
backend; teardown deasserts DTR before restoring saved TTY settings. The runner then retains the configured rate-settle
interval (8 seconds by default) and flushes once more before health sampling or traffic. These delays are setup time,
not time removed from the payload duration, and are not a substitute for readiness verification.

The standalone performance runner and combined runner default to this concurrent six-port sweep, in order:
115200, 128000, 153600, 230400, 256000, 460800, 921600, 1000000, 2000000, and 3000000 baud. The default sweep
does not test above 3 Mbaud. Use `--rates` to select a different list. Concurrent sweeps stop at the first failed
rate; later configured rates are reported as `NOT RUN`, not as failures or missing measurements.

The standalone performance workflow and combined runner require clean HID health before and after each rate,
including unchanged overrun counts. A health failure blocks traffic or fails the completed rate even if payload
verification passed. Raw `stress` invocations opt into these same checks with `--check-hid-health`.
Health collection uses the host API's non-consuming per-channel Feature snapshots and diagnostics, not console
monitor text. Every channel must be closed or ready with current diagnostics and zero recorded RX/configuration
faults; missing reports and HID failures block acceptance. Raw diagnostics, including buffer peaks, are retained.
Transient stale diagnostic captures receive bounded per-channel retries; an exhausted retry remains an explicit
health-query failure, not a clean sample. Retained per-rate summaries include measurement validity and fault history.
Each rate reopens the endpoints, configures line coding, settles, flushes host TTY queues, and then collects its health
baseline.

For controlled budget/threshold experiments, use the
[optional timing-profile procedure](../development/firmware-testing.md#optional-timing-profiles). Keep one variable
different per variant and preserve the ordinary uninstrumented HIL gate. Dated results and limits are recorded in the
[session/tuning check](records/2026-10-22-cdc-uart-session-validation.md), not as universal baud or timing guarantees.
After a failure, reset or reflash the intended image and verify clean health before running a different rate in
a separate invocation. Do not interpret later measurements from older continue-after-failure sweeps as isolated
clean-start results. The runners never reset, reflash, or change regulator voltage automatically.

Benchmark children run with unbuffered Python output, so stdout rate headings and stderr failures retain their
emission order without requiring an external `PYTHONUNBUFFERED` setting. Reports retain per-rate HID summaries.

For an optional concurrent six-port ceiling search, use `--incremental-performance` with the combined runner. It
starts at 460800 baud, increases by 100000 baud, and stops at the first failed rate (capped at 3000000 baud by default).
The summary reports the highest rate where all six streams passed and the first failed rate; that boundary failure is
expected for a ceiling search. Incremental mode can exit successfully after at least one rate passes, and a wrapper can
record `PASS` if its final health check is clean even though boundary rows failed. Inspect every rate and retained HID
summary; neither that exit code nor the aggregate verdict qualifies the failed rate or the required release matrix.
The last passing rate describes only that run's achieved load and conditions. Override the start, step, or cap with
`--incremental-start-rate`,
`--incremental-rate-step`, or `--incremental-max-rate`. The standalone `pico-uart-hil performance --incremental` mode
uses the same options.

The serial setup also checks the host TTY's reported line speed and fails when either direction differs from the
requested rate by 50% or more. This checks driver readback, not an independent measurement of the physical waveform;
payload verification remains the end-to-end check that the firmware and UART link communicate correctly.

| Layer               | Rate sweep                                                         | Duration                        |
| ------------------- | ------------------------------------------------------------------ | ------------------------------- |
| Single direction    | 460800, 600000, 800000, 1000000, 1100000, 1200000                  | 10 s per rate                   |
| Single pair duplex  | 460800, 600000, 800000, 900000, 1000000, 1040000, 1060000, 1080000 | 30 s per rate                   |
| Concurrent six-port | 115200, 128000, 153600, 230400, 256000, 460800, 921600, 1000000, 2000000, 3000000 | 30 s per rate, fresh port setup |

These are explicit test-plan durations, not runner defaults. Pass `--duration` for the intended test level and record
the actual duration; a shorter default run does not establish the same coverage.

Before the pair sweeps, set `PICO_DEVICE_BASE` to the PicoUart `/dev/serial/by-id` path before its `-if00`/`-if04`
interface suffixes.

For the single-direction sweep, test both directions on each crossed pair:

```sh
for stage in stage1 stage2; do
  for direction in a-to-b b-to-a; do
    tools/hil/runner/pair.sh "$stage" --pico-device "$PICO_DEVICE_BASE" \
      --rates 460800,600000,800000,1000000,1100000,1200000 \
      --duration 10 --direction "$direction"
  done
done
```

For synchronized pair-duplex sweeps, test both crossed pairs:

```sh
for stage in stage1 stage2; do
  tools/hil/runner/pair.sh "$stage" --pico-device "$PICO_DEVICE_BASE" \
    --rates 460800,600000,800000,900000,1000000,1040000,1060000,1080000 \
    --duration 30 --direction both
done
```

The pair runner appends the appropriate interface suffix for each stage. Test both loopback endpoints with
`tools/hil/runner/bridge.sh --pico-port <endpoint> --loopback`. The full concurrent command is:

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

For one command that runs both phases and creates a dated record, use the combined runner. It accepts `--pico-cdcN`,
not the standalone performance runner's `--cdcN`, and names the performance payload option explicitly:

```sh
tools/hil/runner/full.sh \
  --pico-cdc0 /dev/serial/by-id/<pico-cdc0> \
  --pico-cdc1 /dev/serial/by-id/<pico-cdc1> \
  --pico-cdc2 /dev/serial/by-id/<pico-cdc2> \
  --pico-cdc3 /dev/serial/by-id/<pico-cdc3> \
  --pico-cdc4 /dev/serial/by-id/<pico-cdc4> \
  --pico-cdc5 /dev/serial/by-id/<pico-cdc5> \
  --rates 115200,128000,153600,230400,256000,460800,921600,1000000,2000000,3000000 \
  --duration 30 --performance-payload-bytes 1024 --timeout 3 \
  --board pico --firmware-version 1.2.3 --firmware-commit <commit> \
  --artifact /path/to/flashed/pico_uart.elf
```

Its performance phase starts only after functional success. Use `--skip-functional` or `--skip-performance` only for
intentional phase-only checks; the combined record is then `PARTIAL`, even if the executed phase exits successfully.
Likewise, a single-stage functional success is not a full-fixture pass. Inspect the recorded scope and result, not only
the process exit code. Continue-after-failure options are diagnostic, not a way to qualify a failed run.

### Six-Port Ceiling Search

When the goal is to find the highest tested rate that passes on all six ports, use the same combined command with
`--incremental-performance` instead of `--rates`. Start at a recently verified all-stream passing rate, choose a
practical step and maximum for the board/clock, and refine the passing/failing bracket with a smaller step if needed.
Each rate includes the configured settling period, so keep the duration and sweep size practical.

The runner stops at the first boundary rate where actual traffic fails. The run is an overall `PASS` for ceiling
measurement only if initial HID health is clean, every lower attempted rate passed on every stream, all six streams
were attempted at the boundary, and final HID sampling succeeded. The boundary row remains `FAIL` and the record says the failure was
expected for the search; report the highest passing tested rate and first failed tested rate. Do not claim an exact
hardware limit or label the overall record `FAIL` for that expected boundary. A dirty HID check before traffic,
serial-setup failure, missing stream outcome, or failure at the first tested rate means no ceiling was measured and
must remain an overall `FAIL`.

The benchmark prints one `TIME <label>: {...}` line per stream with host thread-start, first-send-attempt, and
first-nonempty-read UTC and monotonic timestamps. These are not wire-level first-byte times and are not parsed by
`pico-uart-hil performance`. `--duration` stops starting new blocks; the last in-flight block can finish afterward. A
write or read phase that completes at or after its deadline is a timeout, even if the final bytes arrive at that
boundary. Host scheduling jitter near the deadline can also cause a timeout.

For soak testing, run the concurrent fixture for 60 seconds at 115200 baud, then test individual links at 921600 and
1000000 baud. Capture HID health before and after:

```sh
host/python/.venv/bin/python -m pico_uart monitor --duration 5
host/python/.venv/bin/python -m pico_uart overruns
```

Take these standalone HID samples outside the benchmark, not with another monitor consuming the same board's reports
while the runner is collecting its own evidence.

The 60-second run is a short sanity check, not long-duration stability qualification. Choose and record a duration
appropriate to the intended claim; the [record index](records/README.md#evidence-index) identifies historical longer
runs.

Record every tested rate, including failures. A rate is stable only if every direction and stream passes repeatedly.
Full-speed USB bandwidth limits aggregate throughput; a concurrent failure does not by itself establish a single-UART
baud limit. A data mismatch, unexplained loss, unexpected reset, persistent disconnect, or new RX error is a failure in
a normal fixed-rate qualification run. During a ceiling search, such a failure at the first boundary rate is the expected
stopping observation only when every lower rate passed and boundary traffic was actually attempted; retain and report
the failed row and health evidence.

## Concurrent Recovery And Profiling Coverage

The normal performance sweep verifies USB-paced payloads; it does not by itself exercise the cases below. Run these
as separate, explicitly authorized fixture procedures after flashing the exact image. Use bounded timeouts and record
the board identity, artifact hash, rate, payload/burst size, stall duration, affected channels, and HID samples.
These procedures are not automated by the normal performance runner.

| Case | Exercise | Acceptance and evidence |
| --- | --- | --- |
| All six channels loaded | Run the concurrent fixture and collect both cores' service load, block latency, ring peaks, and faults during traffic; use optional timing instrumentation for service gaps. | Every stream verifies payloads with no new loss/errors at the qualified load. Retain uninstrumented acceptance separately; host pacing is not proof of wire saturation. |
| Host drain stalled | With a bounded independently paced fixture source, pause host reads on one RX channel while the other channels continue; resume before the chosen capacity budget, then test a separate intentional-overflow interval. | Within budget, verify ordered bytes without loss. Beyond budget, require visible dropped-byte accounting and recovery, not lossless delivery; unaffected channels continue without new faults. |
| TX backpressure | Pause a fixture peer with supported CTS in a separately qualified flow-control variant while host OUT writes fill the channel; then resume the peer. | Host writes block or time out explicitly rather than silently losing bytes; admitted bytes remain ordered and other channels continue. Without a supported fixture variant, mark this case not run. |
| Close/reopen during traffic | Close one active channel while the other streams run, then reopen it with staged settings and require HID readiness before sending a fresh, distinguishable marker. | Do not require delivery of deliberately discarded queued bytes. No old-session payload may contaminate the fresh stream; applied settings match and unaffected channels keep passing. |
| Session loss with pending control | Keep a supported line-coding request pending using an authorized controlled busy condition, confirm pending through HID, then unplug or issue a targeted USB bus reset. | Old handles terminate or time out explicitly; after remount all old sessions are closed. Reopen, verify applied settings and a fresh marker, and check that canceled work produces no late timeout. Repeat with all six channels active. |
| Concurrent configuration | Repeatedly change supported settings on other channels while an independently paced channel receives verified traffic. Coordinate both ends of changed links. | Record maximum unaffected-port service gap and both cores' maximum pass times, not just average throughput. Require bounded completion or explicit timeout and no new faults on the unaffected stream. |

For line-coding experiments on crossed pairs, coordinate both ends or use an independent fixture peer; a deliberate
framing mismatch is not an unexplained bridge failure. USB disconnect/reset affects the whole device, so other streams
are expected to terminate in that case. Do not send reset commands or change connections on unrelated devices.
If a pending window cannot be established and observed, report that case as not tested rather than claiming coverage
from an ordinary reset. Preserve intentional overflow results separately from clean-health fixed-rate acceptance.

Use the [buffer measurement procedure](../development/firmware-testing.md#buffer-capacity-measurement) before changing
capacities and the [timing-profile procedure](../development/firmware-testing.md#optional-timing-profiles) before changing
service budgets. Existing short-run evidence is in the
[session/tuning record](records/2026-10-22-cdc-uart-session-validation.md); it does not establish every recovery case
above or qualify the newly built image.

### Optimization Acceptance

Compare baseline and candidate images on both board targets with the same clock, wiring, payload sizes, rates,
durations, and host. Flash and identify each image separately. Change one policy at a time, repeat the baseline to
estimate variation, and retain hashes and raw samples. Include small-payload round trips as well as six-stream
throughput, maximum service gaps, ring peaks, fault deltas, and configuration/reopen behavior. Instrumentation changes
timing; repeat payload acceptance on uninstrumented firmware.

Keep current TX idle/FIFO admission gates, CDC bridge budgets, and PIO DMA thresholds unless a candidate demonstrates
a repeatable improvement beyond baseline variation without correctness regressions. TX pipelining must additionally
preserve CTS stalls, byte ordering, old-format boundary drain, and physical shifter-idle checks before reconfiguration.
Native lock-count regressions prove reduced software work, not an on-wire throughput or latency improvement.
Without authorized hardware testing, these experiments remain pending and no performance improvement is qualified.

## Repeatable Recovery Checks

After flashing and verifying the selected image, use the existing CDC1 and CDC5 loopbacks for bounded development
regressions. Close all six CDC sessions first. The command validates both Linux sysfs endpoint identities against the
explicit USB serial before opening either port. It never resets or flashes the board:

```sh
tools/hil/.venv/bin/pico-uart-hil recovery \
  --serial <usb-serial> --cdc1 /dev/serial/by-id/<cdc1> --cdc5 /dev/serial/by-id/<cdc5> \
  --artifact /path/to/flashed/pico_uart.elf --output build/recovery.json
```

The default `--mode all` runs configuration churn with sibling payload verification, five close/reopen cycles with
distinct old/fresh payloads, a 2048-byte read stall with sibling traffic for at least 250 ms, then an intentional
16384-byte overflow with sibling traffic for at least two seconds. `--mode churn|reopen|stall|overflow` selects one.
Serial payload operations have bounded phase deadlines. HID and host serial setup still depend on OS device calls;
use an outer process deadline in unattended automation, as with the other serial runners.

Acceptance requires verified payloads, current diagnostics, zero unexpected RX/configuration faults on all six ports,
and successful endpoint teardown. During intentional overflow, only CDC1's drop counter may be nonzero: retained
bytes plus drops must equal the transmitted input, retained bytes must be an ordered subsequence, fresh markers must
pass after draining device backlog to quiescence, and recovery must cause no further drops. Host `tcflush` is not
device-backlog recovery. Overflow deliberately leaves retained loss history; reflash before subsequent clean-health
acceptance tests.

The new JSON path is reserved before hardware access and never overwritten. Evidence retains the supplied ELF hash,
live hardware/configuration/version, partial checks, diagnostics, and errors. These are USB-paced loopback tests, not
external-source capacity or CTS qualification. Run the combined functional/performance acceptance separately.

## Startup Observations

Use a separate observation for each authorized cold-power-on, SWD reset, watchdog recovery, or USB reset:

```sh
tools/hil/.venv/bin/pico-uart-hil startup \
  --serial <usb-serial> --event swd-reset --artifact /path/to/flashed/pico_uart.elf \
  --timeout 20 --output build/startup-swd-01.json
```

By default this read-only command neither triggers the event nor opens CDC. For a reset, invoke it immediately after the
independently performed event; for manual power-on, start observation before applying power. Record the event's
actual time and method alongside the JSON. The event label is operator supplied, not proof of a reset.

Each attempt has a subprocess timeout of at most two seconds or the remaining observation deadline. Errors, stderr,
attempt start/end times, and successful hardware/configuration/runtime/channel snapshots are retained. `PASS` requires
a positive live clock and all six channels closed throughout a 12-second observation window (`--stable-seconds`),
within the overall deadline. After the first closed observation, a failed sample or an uptime discontinuity fails
the run rather than retrying into success. Uptime advance from the first sample must match host elapsed time within
one second, allowing 32-bit rollover; this detects observed resets/stalls, not every possible event between samples.
It does not establish initial UART readiness, fault-free traffic, or the expected reset cause. Time is measured from
observer startup, not the physical power/reset edge. A blocked or late HID attempt cannot turn a missed deadline
into a pass.
OS process creation/termination can add latency; this is not a hard real-time host deadline.

Repeat each event type and retain failures as well as successes. Compare uptime/reset cause against elapsed time and
the independently recorded event. Do not increase firmware sleeps or retry windows to conceal unexplained resets.
Cold-start/SWD behavior remains unqualified until the event series and subsequent functional tests pass.

To exercise worker startup rather than only observe closed channels, explicitly add
`--cdc1 /dev/serial/by-id/<cdc1>` on the fixed HW UART1 loopback fixture. This option changes CDC settings and transmits
UART data; it is not read-only. After the first valid closed-channel snapshot, a subprocess checks that the endpoint
is CDC1 on the selected board, opens it at 115200 8N1, awaits the existing HID readiness contract, verifies a 154-byte
loopback payload, and closes/restores the port. The probe runs once, with at most eight seconds or the remaining overall
deadline; any probe error fails the observation immediately rather than retrying into success. The ordinary closed-state
and uptime-stability observations must still pass afterward. Uptime comparisons use timestamps tied to the HID
snapshots rather than probe completion, so a slow probe is not mistaken for a reset.

Use this active variant for startup acceptance; a closed-only pass does not prove that UART worker admission works.
Reserve the default passive variant for diagnosing enumeration or cases where UART transmission is not authorized.
Probe failures retain stderr and partial snapshots. A subprocess deadline may interrupt teardown; on failure inspect
the selected board and reflash before a separate physical test rather than assuming cleanup or a clean session.

## Optimization Decision Gates

Retain the [reviewed production policies](records/2026-10-23-uart-cdc-review-validation.md) unless a workload justifies
another experiment. Do not merge HW/PIO ordering, cache lifecycle readiness, remove control generations, or replace
DMA snapshot ownership checks merely to shorten source. Keep current buffer sizes, bridge budgets, and thresholds.

If small-message PIO throughput matters, repeat the isolated FIFO-admission candidate using interleaved baseline and
candidate images. As a proposed development gate, require at least 5% improvement in the declared target workload
across at least five matched pairs, with no more than 5% regression in p99 or maximum host block latency, no
payload/diagnostic faults, and passing session/configuration recovery. Record block-size/rate distributions and
sample counts; the current benchmark's maximum/mean fields alone do not establish p99. Repeat on both boards before
changing a shared default. Hardware TX pipelining additionally requires the CTS and shifter-idle gates above.
These are experiment criteria, not claims that a candidate has passed them.

Unresolved physical gates remain independently paced ingress during USB stalls/configuration churn, CTS behavior,
RP2350 physical coverage, safe DMA re-arm/full-countdown endurance, and event-specific startup reliability. More
USB-loopback runs cannot close them. Provision the required fixture/source and retain exact-artifact evidence under
the [release matrix](../deployment/releasing.md#required-hil-matrix).

## Measured Six-Port Envelope

The October 9 fixture sweeps establish these observed payload-integrity envelopes under USB-paced traffic for the
tested boards and images, not universal MCU baud limits or continuous full-rate UART capacity. They are historical
evidence, not recommended limits for later firmware; rerun qualification on the intended image and workload.

| Image | Highest tested rate with all six payload streams passing | First failed tested rate | Evidence |
| --- | ---: | ---: | --- |
| Pico, 125 MHz | 256000 baud | 460800 baud | [Rated Pico](records/2026-10-09-070652Z-pico-hil.md) |
| Pico, 250 MHz | 921600 baud | 1000000 baud | [Overclock Pico](records/2026-10-09-071809Z-pico-250mhz-hil.md) |
| Pico 2, 150 MHz | 3000000 baud | None within sweep | [Rated Pico 2](records/2026-10-09-072834Z-pico2-hil.md) |
| Pico 2, 280 MHz | 3000000 baud | None within sweep | [Local 280 MHz build](records/2026-10-09-074700Z-pico2-280mhz-hil.md) |

For the tested rated Pico image, 256000 baud was the highest all-stream payload pass in this matrix,
not an exact maximum: intermediate rates between 256000 and 460800 were not tested. To rerun that historical range,
use `--rates 115200,128000,153600,230400,256000`; the default wider sweep probes beyond those passing observations.
The clock-dependent improvement at 250 MHz is consistent with limited service capacity in the dual-core
USB/UART path at 125 MHz. No core profiling was collected, so a specific multicore defect or CPU bottleneck is
not established. The recorded failures outside this envelope remain failures, not valid payload transfers.

The faster-board aggregate verified throughput approaches roughly 0.47-0.53 MB/s despite higher requested UART
rates. This plateau is consistent with the shared USB full-speed path becoming the limiting resource, including
CDC scheduling, host drain rate, and the benchmark's write/read pacing. It is not a direct measurement of USB
bus saturation or proof that CPU, DMA, or host scheduling contribute nothing. Pico 250 MHz still failed at
1 Mbaud; Pico 2 at 150 MHz and 280 MHz passed all streams through 3 Mbaud in single sweeps. Repeated stability,
per-rate health for these historical runs, and the separate recovery/soak gates are not established by these results.

### USB-Paced Traffic And UART Capacity

Every verified payload in the fixed fixture crosses USB twice: host OUT to the source UART, then destination
UART to host IN. Each stream writes a block and waits for its returned payload before sending the next block.
Shared USB bandwidth and this pacing constrain both the traffic generator and the receiver, leaving possible
idle gaps on the UART wire. A pass at a configured baud therefore proves integrity at the achieved load, not
continuous full-rate operation at that baud.

The standard Pico 2 record's `83413.3 B/s` is one stream's verified payload throughput at a configured 3 Mbaud,
not its physical line speed or a universal maximum. In 8N1, that payload rate corresponds to about 834133 bit/s
of average framing-inclusive traffic, including the effect of idle gaps. The six streams together verified about
0.501 MB/s, corresponding to about 1.002 MB/s of combined USB OUT and IN payload traffic, before USB overhead.
This supports a USB full-speed path bottleneck interpretation, but does not isolate USB bus saturation from
CDC scheduling, host scheduling, CPU/DMA service, or benchmark pacing.

The passes support confidence in 921600-baud operation under comparable workloads. They do not prove continuous
full-rate 921600-baud operation: the standard Pico 2 streams achieved about 73.8-74.4 kB/s at that setting,
below the theoretical 92.16 kB/s per stream for 8N1. The small throughput increase at 1 Mbaud shows a benefit
from the higher setting, not independent proof of sustained UART capacity. Configured rates above 921600 also
passed at the achieved USB-paced load; neither those passes nor the plateau establish a UART maximum.

To qualify continuous UART capacity, use independently paced external UART sources that do not rely on this
device's USB OUT path. Verify received payloads, loss/error counters, duration, and actual wire timing while the
host drains USB IN. If all receive channels are loaded concurrently, retain the shared USB IN bandwidth limit
when interpreting losses; separate UART/backend capacity from end-to-end USB bridge capacity.

## Results and Release Evidence

The combined runner creates a fixed-format, dated record in [records](records/README.md). Standalone functional or
performance runs write to the ignored local log at `build/hil-results.md` by default; preserve separate transcripts when
diagnosing failures. A local log alone does not qualify a release. Release HIL evidence must identify each rated board,
the exact artifact and SHA-256, firmware version and commit, test commands, per-link results, and HID health. Attach or
link the generated record and transcript with the release evidence as described in
[Releasing](../deployment/releasing.md).

`NOT RUN` means the configured rate was not attempted; `NOT REPORTED` means expected per-stream results were absent.
Neither establishes a pass. Preserve incomplete and failed rows, and assess the declared test scope alongside payload
verification and retained health evidence before making a qualification claim.
