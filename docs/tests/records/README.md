# HIL Records

The combined `pico-uart-hil` runner (also exposed by `tools/hil/runner/full.sh`) creates one Markdown record per
invocation in this directory. Use `--record-dir` to choose another directory. `--no-record` suppresses result recording;
it does not skip hardware access, serial configuration, or UART traffic.

## Naming

Generated names use UTC date/time, board target, measured live system clock, and test kind:

```text
YYYY-MM-DD-HHMMSSZ-pico-125mhz-hil.md
YYYY-MM-DD-HHMMSSZ-pico2-150mhz-hil.md
```

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
- For speed sweeps, the 115200-baud six-stream baseline result, highest all-stream passing tested rate, and first failed
  tested rate (or `not reached`)
- Per-rate HID health summaries when collected; rates skipped after a failure are marked `NOT RUN`
- A compact final HID health summary

Interpret sweep results using the
[baseline and speed-ceiling rules](../hil-fixture-test-plan.md#baseline-and-speed-ceiling). Keep stream failures and
HID health results visible in the record.

Reports omit child command lines and verbose monitor transcripts; live test output remains in the terminal.

A local record is evidence, not automatic release qualification. Release evidence must use the exact packaged artifacts
and pass the gates in [Releasing](../../deployment/releasing.md).

## Evidence Index

These summaries preserve each record's reported result. A `PASS` does not fill gaps in measurement or coverage:
performance-only runs do not cover functional or recovery behavior, and requested baud is not measured wire speed.

Pico 2 rows below stop on 2026-10-10 and predate the v0.8.6 Pico release line. No file in this directory is an
exact-artifact Pico 2 run of that line. A Pico `PASS` does not qualify `pico2`.

| Record | Recorded Result | Scope and Limitations |
| --- | --- | --- |
| [2026-10-24 Pico 200 MHz eight-hour 1 Mbaud soak](20261024T223400Z-pico-200mhz-1000000-6port-8h.md) | `PASS` | The v0.8.6 Pico ELF ran six streams at 1 Mbaud for 8 hours on the board-local 200 MHz profile: 12,606,540,800 host-verified CDC bytes, clean before/after HID, and zero overruns. The worktree was dirty; the run ended 2026-10-25 06:34 UTC. |
| [2026-10-24 Pico 200 MHz four-hour 1 Mbaud soak](20261024T183331Z-pico-200mhz-1000000-6port-4h.md) | `PASS` | Same v0.8.6 Pico ELF and six-stream 1 Mbaud workload for 4 hours (6,324,596,736 verified bytes) with clean HID. The clock was not independently captured, and the worktree was dirty. |
| [2026-10-24 v0.8.6 Pico 200 MHz HIL](2026-10-24-181517Z-pico-200mhz-hil.md) | `PASS` | Exact packaged Pico ELF; functional fixture and all six streams passed through 2 Mbaud. CDC5 loopback returned 33 of 1024 bytes at 3 Mbaud; HID health stayed clean with zero overruns/errors. |
| [2026-10-24 v0.8.6 Pico release HIL](2026-10-24-180437Z-pico-125mhz-hil.md) | `PASS` | Exact packaged Pico ELF; all functional links and six streams passed through 2 Mbaud with clean HID health. CDC3→CDC4 returned a short read at 3 Mbaud; no overruns or HID errors were recorded. |
| [2026-10-24 Pico eight-hour 460800 soak](20261024-071715Z-pico-125mhz-460800-8h-soak.md) | `PASS` | Six streams ran at 460800 baud for 8 hours with clean before/after HID. Firmware was reported as 0.0.0; artifact hash is unavailable. |
| [2026-10-24 Pico four-hour 460800 soak](20261024-031611Z-pico-125mhz-460800-4h-soak.md) | `PASS` | Six streams at 460800 baud for 4 hours with clean before/after HID. Firmware was reported as 0.0.0; artifact hash unavailable. |
| [2026-10-23 Pico 3 Mbaud sweep](2026-10-23-220124Z-pico-125mhz-hil.md) | `PASS` | All six streams passed through 2 Mbaud; two streams failed at 3 Mbaud with final zero overruns. Installed firmware commit and artifact hash were not recorded. |
| [2026-10-23 Pico reset-corrected acceptance](2026-10-23-191806Z-pico-125mhz-hil.md) | `PASS` | Explicit core1-reset image; four fixture stages and six streams at 115200/460800, 300 seconds per rate with clean current diagnostics. Active startup/recovery comparisons are in the review validation record; other hardware/reset-event gates remain open. |
| [2026-10-23 Pico extended acceptance](2026-10-23-182232Z-pico-125mhz-hil.md) | `PASS` | Fresh ordinary image; four fixture stages and six streams at 115200/460800, 300 seconds per rate with clean current diagnostics. Does not resolve startup reliability or qualify missing hardware variants. |
| [2026-10-23 UART/CDC review validation](2026-10-23-uart-cdc-review-validation.md) | Scoped development checks | Exact-image RP2040 refill comparisons and recovery; explicit reset-before-core1-launch corrects reproduced post-SWD startup failure in repeated active-probe trials. Refill candidates removed; other reset events and RP2350/CTS/full-countdown qualification remain pending. |
| [2026-10-23 Pico final acceptance](2026-10-23-174031Z-pico-125mhz-hil.md) | `PASS` | Ordinary reviewed image; four functional stages and six streams at 115200/460800, 60 seconds per rate, with current diagnostics and zero faults. Not long-duration qualification. |
| [2026-10-10 Pico 2 twelve-hour soak](2026-10-10-173721Z-pico2-921600-12h.md) | `PASS` | One configured rate under USB-paced load; not continuous wire-capacity or full release-matrix qualification. |
| [2026-10-10 Pico 2 four-hour soak](2026-10-10-013150Z-pico2-921600-4h.md) | `PASS` | One configured rate under USB-paced load; physical flash byte identity was not independently established in the record. |
| [2026-10-09 Pico 2 280 MHz](2026-10-09-074700Z-pico2-280mhz-hil.md) | `PASS` | Local overclock build; one sweep does not establish general overclock stability. |
| [2026-10-09 Pico 2](2026-10-09-072834Z-pico2-hil.md) | `PASS` | One functional/concurrent sweep at USB-paced load, without per-rate health or the complete recovery/soak gates. |
| [2026-10-09 Pico 250 MHz](2026-10-09-071809Z-pico-250mhz-hil.md) | `PASS` | Overclock artifact with documented provenance; all six streams passed through 921600 baud and two failed at 1000000 baud. Per-rate health, recovery, and soak results are absent. |
| [2026-10-09 Pico](2026-10-09-070652Z-pico-hil.md) | `FAIL` | All six streams passed through 256000 baud; the first higher failures were at 460800 baud. The overall run remains `FAIL` because final RX errors were recorded; later rates followed failures and per-rate health was not recorded. |
| [2026-10-08 Pico](2026-10-08-183203Z-pico-hil.md) | `FAIL` | Missing stream results and incomplete performance evidence; functional success does not override the overall failure. |
| [2026-10-08 Pico 2](2026-10-08-173102Z-pico2-hil.md) | `PARTIAL` | Performance only, above the current default sweep, at low achieved load. Its historical “supported rate” label is not a general baud-capability claim. |

Health summaries describe the observations retained; before/after checks do not show continuous error sampling. Paths
to artifacts or transcripts in a tester's local cache are provenance, not repository downloads. Preserve referenced
files with their hashes in an accessible evidence archive when sharing or qualifying a release. Do not backfill unknown
provenance or overwrite historical stream results with later runs. Functional, setup, and health failures remain
failures regardless of the speed boundary.

## Development Notes

Every Markdown file in this directory is linked from this page. These development records add context; they do not
qualify a release.

| Record | Result | Firmware or note |
| --- | --- | --- |
| [2026-10-22 07:49 Pico](2026-10-22-074923Z-pico-hil.md) | `PASS` | 0.8.1; historical run. |
| [2026-10-22 08:02 Pico](2026-10-22-080227Z-pico-hil.md) | `PARTIAL` | 0.8.1; historical run. |
| [2026-10-22 14:06 Pico](2026-10-22-140603Z-pico-hil.md) | `PASS` | 0.0.0. |
| [2026-10-22 18:43 Pico 125 MHz](2026-10-22-184304Z-pico-125mhz-hil.md) | `PARTIAL` | 0.0.0. |
| [2026-10-22 19:29 Pico 125 MHz](2026-10-22-192938Z-pico-125mhz-hil.md) | `PASS` | 0.0.0. |
| [2026-10-22 19:39 Pico 125 MHz](2026-10-22-193909Z-pico-125mhz-hil.md) | `PARTIAL` | 0.0.0. |
| [2026-10-22 20:03 Pico 125 MHz](2026-10-22-200343Z-pico-125mhz-hil.md) | `PASS` | 0.0.0. |
| [2026-10-22 20:29 Pico 125 MHz](2026-10-22-202929Z-pico-125mhz-hil.md) | `PASS` | 0.0.0. |
| [2026-10-22 20:37 Pico 125 MHz](2026-10-22-203712Z-pico-125mhz-hil.md) | `PASS` | 0.0.0. |
| [2026-10-22 20:46 Pico 125 MHz](2026-10-22-204609Z-pico-125mhz-hil.md) | `PASS` | 0.0.0. |
| [2026-10-22 21:31 Pico 125 MHz](2026-10-22-213102Z-pico-125mhz-hil.md) | `PASS` | 0.0.0. |
| [CDC/UART session and tuning check](2026-10-22-cdc-uart-session-validation.md) | Development check | Firmware version not listed. |
| [UART lifecycle optimization check](2026-10-22-uart-lifecycle-optimization.md) | Development check | Firmware version not listed. |
| [2026-10-23 17:00 Pico 125 MHz](2026-10-23-170002Z-pico-125mhz-hil.md) | `PASS` | 0.0.0. |
| [2026-10-23 17:12 Pico 125 MHz](2026-10-23-171221Z-pico-125mhz-hil.md) | `PASS` | 0.0.0. |
| [2026-10-23 17:14 Pico 125 MHz](2026-10-23-171456Z-pico-125mhz-hil.md) | `PASS` | 0.0.0. |
| [2026-10-23 17:16 Pico 125 MHz](2026-10-23-171632Z-pico-125mhz-hil.md) | `PASS` | 0.0.0. |
| [2026-10-23 17:20 Pico 125 MHz](2026-10-23-172029Z-pico-125mhz-hil.md) | `PASS` | 0.0.0. |
| [2026-10-23 17:21 Pico 125 MHz](2026-10-23-172156Z-pico-125mhz-hil.md) | `PASS` | 0.0.0. |
| [2026-10-23 17:24 Pico 125 MHz](2026-10-23-172425Z-pico-125mhz-hil.md) | `PASS` | 0.0.0. |
| [2026-10-23 17:26 Pico 125 MHz](2026-10-23-172600Z-pico-125mhz-hil.md) | `PASS` | 0.0.0. |
| [2026-10-23 17:27 Pico 125 MHz](2026-10-23-172735Z-pico-125mhz-hil.md) | `PASS` | 0.0.0. |
