# HIL Fixture Test Plan

Use this plan to qualify the fixed PicoUart HIL fixture. It covers functional validation first, then performance testing
on the same wiring. Physical hardware is required; CI runs host tests and firmware builds only.

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
uv run --project tools/hil --extra test pico-uart-hil-functional \
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
is not a pass. Test each pair individually, then run all six streams concurrently. Use a separate process for each
candidate rate so line-coding changes settle before traffic begins.

| Layer               | Rate sweep                                                         | Duration                           |
| ------------------- | ------------------------------------------------------------------ | ---------------------------------- |
| Single direction    | 460800, 600000, 800000, 1000000, 1100000, 1200000                  | 10 s per rate                      |
| Single pair duplex  | 460800, 600000, 800000, 900000, 1000000, 1040000, 1060000, 1080000 | 30 s per rate                      |
| Concurrent six-port | 460800, 500000, 600000                                             | 30 s per rate, independent process |

For synchronized pair diagnostics, use `pico-uart-hil-pair stage1` for CDC0↔CDC2 and `stage2` for CDC3↔CDC4. Test both
loopback endpoints with `pico-uart-hil-bridge --loopback`. The full concurrent command is:

```sh
uv run --project tools/hil --extra test pico-uart-hil-performance \
  --cdc0 /dev/serial/by-id/<pico-cdc0> \
  --cdc1 /dev/serial/by-id/<pico-cdc1> \
  --cdc2 /dev/serial/by-id/<pico-cdc2> \
  --cdc3 /dev/serial/by-id/<pico-cdc3> \
  --cdc4 /dev/serial/by-id/<pico-cdc4> \
  --cdc5 /dev/serial/by-id/<pico-cdc5> \
  --rates 460800,500000,600000 --duration 30 --payload-bytes 1024 --timeout 3 \
  --board pico --firmware-version 1.2.3 --firmware-commit <commit>
```

For one command that runs both phases, use `uv run --project tools/hil --extra test pico-uart-hil` with the same
six `--pico-cdcN` arguments. Its performance phase starts only after functional pass. Pass `--skip-functional` or
`--skip-performance` only when intentionally running one phase.

The benchmark prints one `TIME <label>: {...}` line per stream with host thread-start, first-send-attempt, and
first-nonempty-read UTC and monotonic timestamps. These are not wire-level first-byte times and are not parsed by
`pico-uart-hil-performance`. `--duration` stops starting new blocks; the last in-flight block can finish afterward. A
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

## Results and Release Evidence

`pico-uart-hil` creates a fixed-format, dated record in [records](records/README.md). Standalone functional or
performance runs append to the ignored local log at `build/hil-results.md`; preserve separate transcripts when
diagnosing failures. A local log alone does not qualify a release. Release HIL evidence must identify each rated board,
the exact artifact and SHA-256, firmware version and commit, test commands, per-link results, and HID health. Attach or
link the generated record and transcript with the release evidence as described in [Releasing](../releasing.md).
