# HIL Test Records

The combined `pico-uart-hil` runner (also exposed by `tools/hil/runner/full.sh`) creates one Markdown record per
invocation in this directory; `--record-dir` chooses another. Single-phase runs and `pico-uart-hil functional` /
`pico-uart-hil performance` append to the ignored local log `build/hil-results.md` instead.

## Naming

Name records by UTC start time, board target, measured live system clock, and test kind:

```text
YYYY-MM-DD-HHMMSSZ-pico-125mhz-hil.md
YYYY-MM-DD-HHMMSSZ-pico2-150mhz-hil.md
YYYY-MM-DD-HHMMSSZ-pico-125mhz-460800-6port-8h-soak.md
```

- Runner records end in `-hil.md`. Manual soaks add baud rate, port count, duration, and `-soak.md`.
- Read the clock with `pico-uart hardware`. If it cannot be read, use `clock-unknown`; never infer it.
- Narrative summaries use `YYYY-MM-DD-topic.md`. Older records may be date-only when the start time was not recorded.
- The runner adds a numeric suffix rather than overwriting an existing name.

## Record Formats

Current runner reports use the `PicoUart HIL Record` heading and include:

- Overall `PASS`, `FAIL`, or `PARTIAL` status
- Board, UTC start time, firmware version, firmware commit, runner Git commit, and worktree state
- Live system clock in combined records
- Artifact name and SHA-256 when supplied
- A functional summary table with each fixture link, result, and verified byte count
- A concurrent performance table with each baud rate, stream, result, verified bytes, and throughput/error
- For speed sweeps, the 115200-baud six-stream baseline result, the highest rate all six streams passed, and the first
  rate that failed (or `not reached`)
- A compact per-rate HID summary; failed rates retain their health details
- A compact final HID health summary

Interpret sweep results using the
[baseline and speed-ceiling rules](../hil-fixture-test-plan.md#baseline-and-speed-ceiling). Keep stream failures and
HID health results visible.

Reports omit child command lines and verbose monitor transcripts; live test output remains in the terminal.

Historical hand-written records use the `PicoUart HIL Results` heading and may organize fields differently. Soak
records also use two legacy timestamp styles (`Test start` / `Report generated` and `Test start` / `Run ended`).
Older runner records, including the v0.8.6 runs, list every per-rate HID check in full. Some include health bytes such
as `0x11` and `0x31`, preserved as reported with no verified mapping in the current report contract. Interpret current
reports using the [HID reference](../../design/usb/hid-report-reference.md).

Release qualification uses the gates in [Releasing](../../deployment/releasing.md).

## Evidence Index

Performance-only runs do not cover functional or recovery behavior. Configured baud is not a wire-rate measurement.

Pico 2 rows below stop on 2026-10-10. No file in this directory is an exact-artifact Pico 2 run for the current
release. A Pico `PASS` does not qualify `pico2`.

### Recent Records

| Record | Result and coverage | Artifact or caveat |
| --- | --- | --- |
| [Pico 200 MHz eight-hour 1 Mbaud soak](2026-10-24-223400Z-pico-200mhz-1000000-6port-8h-soak.md) | `PASS`; 12.6 billion verified CDC bytes, clean HID, zero overruns. | v0.8.6 ELF; dirty worktree. |
| [Pico four-hour 1 Mbaud soak](2026-10-24-183331Z-pico-clock-unknown-1000000-6port-4h-soak.md) | `PASS`; 6.3 billion verified bytes, clean HID. | Same v0.8.6 ELF; clock not independently captured, dirty worktree. |
| [v0.8.6 Pico 200 MHz HIL](2026-10-24-181517Z-pico-200mhz-hil.md) | `PASS`; fixture and six streams through 2 Mbaud. | Exact packaged ELF; CDC5 returned a short read at 3 Mbaud. |
| [v0.8.6 Pico release HIL](2026-10-24-180437Z-pico-125mhz-hil.md) | `PASS`; fixture and six streams through 2 Mbaud. | Exact packaged ELF; CDC3→CDC4 returned a short read at 3 Mbaud. |
| [Pico eight-hour 460800 soak](2026-10-24-071715Z-pico-125mhz-460800-6port-8h-soak.md) | `PASS`; six streams, clean before/after HID. | Firmware version `0.0.0`; no artifact hash. |
| [Pico four-hour 460800 soak](2026-10-24-031611Z-pico-125mhz-460800-6port-4h-soak.md) | `PASS`; six streams, clean before/after HID. | Firmware version `0.0.0`; no artifact hash. |
| [Pico 3 Mbaud sweep](2026-10-23-220124Z-pico-125mhz-hil.md) | `PASS`; all streams through 2 Mbaud, two failed at 3 Mbaud. | Installed commit and artifact hash absent. |
| [Pico reset-corrected acceptance](2026-10-23-191806Z-pico-125mhz-hil.md) | `PASS`; fixture and six streams at 115200/460800 for 300 s per rate. | Clean diagnostics; other reset events and boards remain open. |
| [Pico extended acceptance](2026-10-23-182232Z-pico-125mhz-hil.md) | `PASS`; fixture and six streams at 115200/460800 for 300 s per rate. | Clean diagnostics; startup reliability remains open. |
| [UART/CDC review validation](2026-10-23-uart-cdc-review-validation.md) | Scoped RP2040 refill comparisons and recovery. | Refill candidates removed; RP2350, CTS, and other reset cases remain open. |
| [Pico final acceptance](2026-10-23-174031Z-pico-125mhz-hil.md) | `PASS`; fixture and six streams at 115200/460800 for 60 s per rate. | Clean diagnostics; no long soak. |

### Historical Records

| Record | Result and coverage | Artifact or caveat |
| --- | --- | --- |
| [Pico 2 twelve-hour soak](2026-10-10-173721Z-pico2-921600-6port-12h-soak.md) | `PASS`; one USB-paced rate. | Does not cover the release matrix or continuous wire capacity. |
| [Pico 2 four-hour soak](2026-10-10-013150Z-pico2-921600-6port-4h-soak.md) | `PASS`; one USB-paced rate. | Board flash was not matched to the artifact. |
| [Pico 2 280 MHz](2026-10-09-074700Z-pico2-280mhz-hil.md) | `PASS`; local overclock sweep. | No stability run. |
| [Pico 2](2026-10-09-072834Z-pico2-hil.md) | `PASS`; one functional/concurrent sweep. | USB-paced load; per-rate health and recovery/soak evidence absent. |
| [Pico 250 MHz](2026-10-09-071809Z-pico-250mhz-hil.md) | `PASS` through 921600; two streams failed at 1000000. | Overclock artifact; per-rate health and recovery evidence absent. |
| [Pico](2026-10-09-070652Z-pico-hil.md) | `FAIL`; streams passed through 256000, then failed at 460800. | Final RX errors; later rates and per-rate health absent. |
| [Pico](2026-10-08-183203Z-pico-hil.md) | `FAIL`; incomplete stream results and performance evidence. | Functional success does not change the overall result. |
| [Pico 2](2026-10-08-173102Z-pico2-hil.md) | `PARTIAL`; performance-only, above the current default sweep. | Low achieved load; no release qualification. |

Health summaries describe the observations retained; before/after checks are not continuous sampling.

### Development History

Development runner output from October 22–23 is archived separately from release evidence in the
[archive index](archive/README.md); narrative records link directly to the runs they discuss. Narrative summaries:
[UART lifecycle optimization](2026-10-22-uart-lifecycle-optimization.md) and
[CDC/UART session validation](2026-10-22-cdc-uart-session-validation.md).

## Evidence Retention

Some records refer to artifacts or transcripts in a tester's local cache rather than files in this repository. Preserve
those files with their hashes in an accessible evidence archive before using them for release qualification. Keep
historical provenance gaps visible, and do not overwrite recorded results with later runs.
