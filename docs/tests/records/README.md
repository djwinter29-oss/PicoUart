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

For an ascending speed sweep, the overall result is `PASS` when all six streams pass at 115200 baud and HID health is
clean. A failure at a higher tested rate is the observed speed boundary, not a failed baseline test; retain that
`FAIL` in its stream row and identify it as the first failed tested rate. Report the highest rate where all six streams
passed. If 115200 fails or is missing, the baseline did not pass; do not infer a ceiling from higher-rate results.
Health failures remain failures.

Reports intentionally omit child command lines and verbose monitor transcripts. The terminal still shows live test
output while a run is in progress. Standalone phase entries are appended to the ignored local log at
`build/hil-results.md`.

A local record is evidence, not automatic release qualification. Release evidence must use the exact packaged artifacts
and pass the gates in [Releasing](../../deployment/releasing.md).

## Evidence Index

The measurements below are historical evidence interpreted using the current baseline/ceiling convention; the result
tables preserve the original stream outcomes. Missing measurements must not be inferred from a `PASS`.
Performance-only runs do not establish functional or recovery coverage, and requested baud is not independently
measured wire speed.

| Record | Recorded Result | Scope And Limitations |
| --- | --- | --- |
| [2026-10-24 v0.8.6 Pico 200 MHz HIL](2026-10-24-181517Z-pico-200mhz-hil.md) | `PASS` | Exact packaged Pico ELF with the board-local 200 MHz profile; functional fixture and all six streams passed through 2 Mbaud. At 3 Mbaud, CDC5 loopback failed a short read (33 of 1024 bytes), establishing the first observed boundary; HID health remained clean with zero overruns/errors. |
| [2026-10-24 v0.8.6 Pico release HIL](2026-10-24-180437Z-pico-125mhz-hil.md) | `PASS` | Exact packaged Pico ELF; all functional links and all six streams passed through 2 Mbaud with clean HID health. At 3 Mbaud, CDC3→CDC4 failed a short read, establishing the first observed boundary; zero overruns or HID errors were recorded. |
| [2026-10-23 Pico 3 Mbaud sweep](2026-10-23-220124Z-pico-125mhz-hil.md) | `PASS` | All six streams passed through 2 Mbaud; two streams failed at 3 Mbaud with final zero overruns. The installed firmware commit and artifact hash were unavailable, so this is not release-artifact qualification. |
| [2026-10-23 Pico reset-corrected acceptance](2026-10-23-191806Z-pico-125mhz-hil.md) | `PASS` | Explicit core1-reset image; four fixture stages and six streams at 115200/460800, 300 seconds per rate with clean current diagnostics. Active startup/recovery comparisons are in the review validation record; other hardware/reset-event gates remain open. |
| [2026-10-23 Pico extended acceptance](2026-10-23-182232Z-pico-125mhz-hil.md) | `PASS` | Fresh ordinary image; four fixture stages and six streams at 115200/460800, 300 seconds per rate with clean current diagnostics. Does not resolve startup reliability or qualify missing hardware variants. |
| [2026-10-23 UART/CDC review validation](2026-10-23-uart-cdc-review-validation.md) | Scoped development checks | Exact-image RP2040 refill comparisons and recovery; explicit reset-before-core1-launch corrects reproduced post-SWD startup failure in repeated active-probe trials. Refill candidates removed; other reset events and RP2350/CTS/full-countdown qualification remain pending. |
| [2026-10-23 Pico final acceptance](2026-10-23-174031Z-pico-125mhz-hil.md) | `PASS` | Ordinary reviewed image; four functional stages and six streams at 115200/460800, 60 seconds per rate, with current diagnostics and zero faults. Not long-duration qualification. |
| [2026-10-08 Pico 2](2026-10-08-173102Z-pico2-hil.md) | `PARTIAL` | Performance only, above the current default sweep, at low achieved load. Its historical “supported rate” label is not a general baud-capability claim. |
| [2026-10-08 Pico](2026-10-08-183203Z-pico-hil.md) | `FAIL` | Missing stream results and incomplete performance evidence; functional success does not override the overall failure. |
| [2026-10-09 Pico](2026-10-09-070652Z-pico-hil.md) | `FAIL` | All six streams passed through 256000 baud; the first higher failures were at 460800 baud. The overall run remains `FAIL` because final RX errors were recorded; later rates followed failures and per-rate health was not recorded. |
| [2026-10-09 Pico 250 MHz](2026-10-09-071809Z-pico-250mhz-hil.md) | `PASS` | Overclock artifact with documented provenance; all six streams passed through 921600 baud and two failed at 1000000 baud. Per-rate health, recovery, and soak coverage are missing, so this does not establish release qualification. |
| [2026-10-09 Pico 2](2026-10-09-072834Z-pico2-hil.md) | `PASS` | One functional/concurrent sweep at USB-paced load, without per-rate health or the complete recovery/soak gates. |
| [2026-10-09 Pico 2 280 MHz](2026-10-09-074700Z-pico2-280mhz-hil.md) | `PASS` | Local overclock build, not the packaged release artifact; one sweep does not establish general overclock stability. |
| [2026-10-10 Pico 2 four-hour soak](2026-10-10-013150Z-pico2-921600-4h.md) | `PASS` | One configured rate under USB-paced load; physical flash byte identity was not independently established in the record. |
| [2026-10-10 Pico 2 twelve-hour soak](2026-10-10-173721Z-pico2-921600-12h.md) | `PASS` | One configured rate under USB-paced load; not continuous wire-capacity or full release-matrix qualification. |

Health summaries describe the observations actually retained; do not infer continuous error sampling from before/after
checks. Paths to artifacts or transcripts in a tester's local cache are provenance, not repository downloads. Preserve
the referenced files with their hashes in an accessible evidence archive when sharing or qualifying a release.
Do not backfill unknown provenance or overwrite historical stream results with later runs. A higher-rate boundary
failure does not change a clean 115200-baseline speed sweep to `FAIL`; independent health, setup, or functional failures
remain failures.
