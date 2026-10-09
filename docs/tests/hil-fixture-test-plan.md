# HIL Fixture Test Plan

Use this plan to qualify the fixed PicoUart HIL fixture. It covers functional validation first, then performance testing
on the same wiring. Physical hardware is required; CI runs host tests and firmware builds only.

This is the baseline fixture plan, not the complete release gate. Release qualification also requires the recovery and
stress checks listed in [Releasing](../releasing.md#required-hil-matrix).

## Setup

Follow [HIL Fixture Setup](hil-fixture-setup.md) and install both crossed pairs and both loopbacks before testing. The
[UART Pinout and Wiring](../uart-pinout.md) document is the GPIO source of truth. Use the manual Debug Probe
external-peer check only when needed; it is not part of HIL pass criteria.

Test both board targets when hardware is available:

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
  --board pico --firmware-version 1.2.3 --firmware-commit <commit>
```

Functional pass criteria:

- CDC0 through CDC5 and HID enumerate.
- Both crossed pairs pass in both directions; both loopbacks pass.
- No unexpected reset, disconnect, serial framing error, new HID `rx_error`, or `control_error` occurs.
- RX overflow counts do not increase.

Use `--stage 1`, `--stage 2`, `--stage 3`, or `--stage 4` to diagnose one fixture link. A single-stage run is `PARTIAL`,
not a full functional pass. Use `--continue-on-failure` to collect all stage results and `--no-record` for a dry run.

## Performance Pass

Run performance testing only after functional pass. Every stream verifies returned payload bytes; traffic activity alone
is not a pass. Test each pair individually, then run all six streams concurrently. The pair and concurrent runners
process comma-separated rates sequentially in one process; they reopen and reconfigure the ports and wait for the settle
interval before testing each rate.

The standalone performance runner and combined runner default to this concurrent six-port sweep, in order:
115200, 128000, 153600, 230400, 256000, 460800, 921600, 1000000, 2000000, and 3000000 baud. The default sweep
does not test above 3 Mbaud. Use `--rates` to select a different list. Concurrent sweeps stop at the first failed
rate; later configured rates are reported as `NOT RUN`, not as failures or missing measurements.

The standalone performance workflow and combined runner require clean HID health before and after each rate,
including unchanged overrun counts. A health failure blocks traffic or fails the completed rate even if payload
verification passed. Raw `stress` invocations opt into these same checks with `--check-hid-health`.
Each rate reopens the endpoints, configures line coding, and settles before its health baseline is collected.
After a failure, reset or reflash the intended image and verify clean health before running a different rate in
a separate invocation. Do not interpret later measurements from older continue-after-failure sweeps as isolated
clean-start results. The runners never reset, reflash, or change regulator voltage automatically.

Benchmark children run with unbuffered Python output, so stdout rate headings and stderr failures retain their
emission order without requiring an external `PYTHONUNBUFFERED` setting. Reports retain per-rate HID summaries.

For an optional concurrent six-port ceiling search, use `--incremental-performance` with the combined runner. It
starts at 460800 baud, increases by 100000 baud, and stops at the first failed rate (capped at 3000000 baud by default).
The summary reports the highest rate where all six streams passed and the first failed rate; that boundary failure is
expected for a ceiling search. Override the start, step, or cap with `--incremental-start-rate`,
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
  --board pico --firmware-version 1.2.3 --firmware-commit <commit>
```

For one command that runs both phases, use `tools/hil/runner/full.sh` with the same
six `--pico-cdcN` arguments. Its performance phase starts only after functional pass. Pass `--skip-functional` or
`--skip-performance` only when intentionally running one phase.

The benchmark prints one `TIME <label>: {...}` line per stream with host thread-start, first-send-attempt, and
first-nonempty-read UTC and monotonic timestamps. These are not wire-level first-byte times and are not parsed by
`pico-uart-hil performance`. `--duration` stops starting new blocks; the last in-flight block can finish afterward. A
write or read phase that completes at or after its deadline is a timeout, even if the final bytes arrive at that
boundary. Host scheduling jitter near the deadline can also cause a timeout.

For soak testing, run the concurrent fixture for 60 seconds at 115200 baud, then test individual links at 921600 and
1000000 baud. Capture HID health before and after:

```sh
python3 -m pico_uart monitor --duration 5
python3 -m pico_uart overruns
```

Record every tested rate, including failures. A rate is stable only if every direction and stream passes repeatedly.
Full-speed USB bandwidth limits aggregate throughput; a concurrent failure does not by itself establish a single-UART
baud limit. A data mismatch, unexplained loss, unexpected reset, persistent disconnect, or new RX error is a failure.

## Measured Six-Port Envelope

The October 9 fixture sweeps establish these observed payload-integrity envelopes under USB-paced traffic for the
tested boards and images, not universal MCU baud limits or continuous full-rate UART capacity. Use the passing
range for comparable workloads; higher rates are ceiling-search tests.

| Image | Highest tested rate with all six payload streams passing | First failed tested rate | Evidence |
| --- | ---: | ---: | --- |
| Pico, 125 MHz | 256000 baud | 460800 baud | [Rated Pico](records/2026-10-09-070652Z-pico-hil.md) |
| Pico, 250 MHz | 921600 baud | 1000000 baud | [Overclock Pico](records/2026-10-09-071809Z-pico-250mhz-hil.md) |
| Pico 2, 150 MHz | 3000000 baud | None within sweep | [Rated Pico 2](records/2026-10-09-072834Z-pico2-hil.md) |
| Pico 2, 280 MHz | 3000000 baud | None within sweep | [Local 280 MHz build](records/2026-10-09-074700Z-pico2-280mhz-hil.md) |

For the rated Pico, 256000 baud is the documented conservative six-port operating envelope from this matrix,
not an exact maximum: intermediate rates between 256000 and 460800 were not tested. To verify that envelope,
use `--rates 115200,128000,153600,230400,256000`; the default wider sweep intentionally probes unsupported rates.
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

`pico-uart-hil` creates a fixed-format, dated record in [records](records/README.md). Functional or
performance runs append to the ignored local log at `build/hil-results.md`; preserve separate transcripts when
diagnosing failures. A local log alone does not qualify a release. Release HIL evidence must identify each rated board,
the exact artifact and SHA-256, firmware version and commit, test commands, per-link results, and HID health. Attach or
link the generated record and transcript with the release evidence as described in [Releasing](../releasing.md).
