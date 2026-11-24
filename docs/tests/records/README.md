# HIL Records

The combined `pico-uart-hil` runner (also exposed by `tools/hil/runner/full.sh`) creates one Markdown record per
invocation in this directory. Use `--record-dir` to choose another directory.

## Naming

New records use UTC date/time, board target, measured live system clock, and test kind:

```text
YYYY-MM-DD-HHMMSSZ-pico-125mhz-hil.md
YYYY-MM-DD-HHMMSSZ-pico2-150mhz-hil.md
```

Older records may use either `YYYYMMDDTHHMMSSZ` or `YYYYMMDD-HHMMSSZ` for the date and time.
If the clock could not be read, the filename uses `clock-unknown`; the report must not infer a clock. If a name already
exists, the runner adds a numeric suffix rather than overwriting it. Single-phase runs and
`pico-uart-hil functional` / `pico-uart-hil performance` invocations append to the ignored local log at
`build/hil-results.md` instead.

## Record Format

Combined and standalone functional/performance reports use the same concise format. Each entry contains:

- Overall `PASS`, `FAIL`, or `PARTIAL` status
- Board, UTC run time, firmware version, firmware commit, runner Git commit, and worktree state
- Live system clock in combined records
- Artifact name and SHA-256 when supplied
- A functional summary table with each fixture link, result, and verified byte count
- A concurrent performance table with each baud rate, stream, result, verified bytes, and throughput/error
- For speed sweeps, the 115200-baud six-stream baseline result, the highest rate all six streams passed, and the first
  rate that failed (or `not reached`)
- Per-rate HID health summaries when collected; rates skipped after a failure are marked `NOT RUN`
- A compact final HID health summary

Interpret sweep results using the
[baseline and speed-ceiling rules](../hil-fixture-test-plan.md#baseline-and-speed-ceiling). Keep stream failures and
HID health results visible in the record.

Reports omit child command lines and verbose monitor transcripts; live test output remains in the terminal.

Release qualification uses the gates in [Releasing](../../deployment/releasing.md).

## Evidence Index

The newest versioned release record is for v0.8.6 (2026-10-24). Repository tags now reach v0.12.0, but this directory
has no exact-artifact HIL record for v0.9.0 or later. Performance-only runs do not cover functional or recovery
behavior; configured baud is not a wire-rate measurement.

Pico 2 rows below stop on 2026-10-10 and predate the v0.8.6 Pico release line. No file in this directory is an
exact-artifact Pico 2 run of that line. A Pico `PASS` does not qualify `pico2`.

Every Markdown file in this directory is linked from this page.

### Recent Records

| Record | Result and coverage | Artifact or caveat |
| --- | --- | --- |
| [Pico 200 MHz eight-hour 1 Mbaud soak](20261024T223400Z-pico-200mhz-1000000-6port-8h.md) | `PASS`; 12.6 billion verified CDC bytes, clean HID, zero overruns. | v0.8.6 ELF; dirty worktree. |
| [Pico 200 MHz four-hour 1 Mbaud soak](20261024T183331Z-pico-200mhz-1000000-6port-4h.md) | `PASS`; 6.3 billion verified bytes, clean HID. | Same v0.8.6 ELF; clock not independently captured, dirty worktree. |
| [v0.8.6 Pico 200 MHz HIL](2026-10-24-181517Z-pico-200mhz-hil.md) | `PASS`; fixture and six streams through 2 Mbaud. | Exact packaged ELF; CDC5 returned a short read at 3 Mbaud. |
| [v0.8.6 Pico release HIL](2026-10-24-180437Z-pico-125mhz-hil.md) | `PASS`; fixture and six streams through 2 Mbaud. | Exact packaged ELF; CDC3→CDC4 returned a short read at 3 Mbaud. |
| [Pico eight-hour 460800 soak](20261024-071715Z-pico-125mhz-460800-8h-soak.md) | `PASS`; six streams, clean before/after HID. | Firmware version `0.0.0`; no artifact hash. |
| [Pico four-hour 460800 soak](20261024-031611Z-pico-125mhz-460800-4h-soak.md) | `PASS`; six streams, clean before/after HID. | Firmware version `0.0.0`; no artifact hash. |
| [Pico 3 Mbaud sweep](2026-10-23-220124Z-pico-125mhz-hil.md) | `PASS`; all streams through 2 Mbaud, two failed at 3 Mbaud. | Installed commit and artifact hash absent. |
| [Pico reset-corrected acceptance](2026-10-23-191806Z-pico-125mhz-hil.md) | `PASS`; fixture and six streams at 115200/460800 for 300 s per rate. | Clean diagnostics; other reset events and boards remain open. |
| [Pico extended acceptance](2026-10-23-182232Z-pico-125mhz-hil.md) | `PASS`; fixture and six streams at 115200/460800 for 300 s per rate. | Clean diagnostics; startup reliability remains open. |
| [UART/CDC review validation](2026-10-23-uart-cdc-review-validation.md) | Scoped RP2040 refill comparisons and recovery. | Refill candidates removed; RP2350, CTS, and other reset cases remain open. |
| [Pico final acceptance](2026-10-23-174031Z-pico-125mhz-hil.md) | `PASS`; fixture and six streams at 115200/460800 for 60 s per rate. | Clean diagnostics; no long soak. |

### Historical Records

| Record | Result and coverage | Artifact or caveat |
| --- | --- | --- |
| [Pico 2 twelve-hour soak](2026-10-10-173721Z-pico2-921600-12h.md) | `PASS`; one USB-paced rate. | Does not cover the release matrix or continuous wire capacity. |
| [Pico 2 four-hour soak](2026-10-10-013150Z-pico2-921600-4h.md) | `PASS`; one USB-paced rate. | Board flash was not matched to the artifact. |
| [Pico 2 280 MHz](2026-10-09-074700Z-pico2-280mhz-hil.md) | `PASS`; local overclock sweep. | No stability run. |
| [Pico 2](2026-10-09-072834Z-pico2-hil.md) | `PASS`; one functional/concurrent sweep. | USB-paced load; per-rate health and recovery/soak evidence absent. |
| [Pico 250 MHz](2026-10-09-071809Z-pico-250mhz-hil.md) | `PASS` through 921600; two streams failed at 1000000. | Overclock artifact; per-rate health and recovery evidence absent. |
| [Pico](2026-10-09-070652Z-pico-hil.md) | `FAIL`; streams passed through 256000, then failed at 460800. | Final RX errors; later rates and per-rate health absent. |
| [Pico](2026-10-08-183203Z-pico-hil.md) | `FAIL`; incomplete stream results and performance evidence. | Functional success does not change the overall result. |
| [Pico 2](2026-10-08-173102Z-pico2-hil.md) | `PARTIAL`; performance-only, above the current default sweep. | Low achieved load; no release qualification. |

Health summaries describe the observations retained; before/after checks are not continuous sampling.

### Development Runs

These Pico runs from October 22–23 are development history, not release evidence.

| Date | Runs and notes |
| --- | --- |
| Oct 22 | [07:49](2026-10-22-074923Z-pico-hil.md) `PASS`; [08:02](2026-10-22-080227Z-pico-hil.md) `PARTIAL`; [14:06](2026-10-22-140603Z-pico-hil.md) `PASS`; [18:43](2026-10-22-184304Z-pico-125mhz-hil.md) `PARTIAL`; [19:29](2026-10-22-192938Z-pico-125mhz-hil.md) `PASS`; [19:39](2026-10-22-193909Z-pico-125mhz-hil.md) `PARTIAL`; [20:03](2026-10-22-200343Z-pico-125mhz-hil.md) and [20:46](2026-10-22-204609Z-pico-125mhz-hil.md) `PASS`. |
| Oct 22 | [UART lifecycle optimization](2026-10-22-uart-lifecycle-optimization.md): passing [baseline](2026-10-22-202929Z-pico-125mhz-hil.md) and [candidate](2026-10-22-203712Z-pico-125mhz-hil.md). |
| Oct 22 | [CDC/UART session and tuning check](2026-10-22-cdc-uart-session-validation.md); final full run [passed](2026-10-22-213102Z-pico-125mhz-hil.md). |
| Oct 23 | Before final acceptance, all `PASS`: [17:00](2026-10-23-170002Z-pico-125mhz-hil.md), [17:12](2026-10-23-171221Z-pico-125mhz-hil.md), [17:14](2026-10-23-171456Z-pico-125mhz-hil.md), [17:16](2026-10-23-171632Z-pico-125mhz-hil.md), [17:20](2026-10-23-172029Z-pico-125mhz-hil.md), [17:21](2026-10-23-172156Z-pico-125mhz-hil.md), [17:24](2026-10-23-172425Z-pico-125mhz-hil.md), [17:26](2026-10-23-172600Z-pico-125mhz-hil.md), and [17:27](2026-10-23-172735Z-pico-125mhz-hil.md). |

## Historical Throughput

The October 9 six-port sweeps used USB-paced traffic. On the faster Pico boards, total verified throughput levelled
around 0.47-0.53 MB/s. Each payload crossed USB twice and each stream waited for its echo, so the results describe the
test path and clock profile. The 250 MHz Pico passed higher rates than the 125 MHz board; the bottleneck was not
profiled. A pass confirms payload integrity at the achieved load.

## Tuning Notes

The variants tried so far did not beat the defaults. Halving the bridge pass budget or PIO TX DMA threshold left
throughput unchanged and slightly raised worst block latency
([tuning check](2026-10-22-cdc-uart-session-validation.md)). Removing a TX admission check slowed small hardware-UART
blocks at 115200; PIO gained 5–8% at 460800 on one board and few samples, with no gain at 115200. Both variants were
removed ([review record](2026-10-23-uart-cdc-review-validation.md#matched-refill-experiments)).

## Evidence Retention

Some records refer to artifacts or transcripts in a tester's local cache rather than files in this repository. Preserve
those files with their hashes in an accessible evidence archive before using them for release qualification. Keep
historical provenance gaps visible, and do not overwrite recorded results with later runs.
