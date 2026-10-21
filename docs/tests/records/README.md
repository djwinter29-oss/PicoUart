# HIL Records

The combined `pico-uart-hil` runner (also exposed by `tools/hil/runner/full.sh`) creates one Markdown record per
invocation in this directory. Use `--record-dir` to choose another directory. `--no-record` suppresses result recording;
it does not skip hardware access, serial configuration, or UART traffic.

## Naming

Generated names use UTC date/time, board target or variant label, and test kind:

```text
YYYY-MM-DD-HHMMSSZ-pico-hil.md
YYYY-MM-DD-HHMMSSZ-pico2-hil.md
```

If a name already exists, the runner adds a numeric suffix rather than overwriting it. Single-phase runs and
`pico-uart-hil functional` / `pico-uart-hil performance` invocations append to the ignored local log at
`build/hil-results.md` instead.

## Record Format

Combined and standalone functional/performance reports use the same concise format. Each entry contains:

- Overall `PASS`, `FAIL`, or `PARTIAL` status
- Board, UTC run time, firmware version, firmware commit, runner Git commit, and worktree state
- Artifact name and SHA-256 when supplied
- A functional summary table with each fixture link, result, and verified byte count
- A concurrent performance table with each baud rate, stream, result, verified bytes, and throughput/error
- Per-rate HID health summaries when collected; rates skipped after a failure are marked `NOT RUN`
- A compact final HID health summary

Reports intentionally omit child command lines and verbose monitor transcripts. The terminal still shows live test
output while a run is in progress. Standalone phase entries are appended to the ignored local log at
`build/hil-results.md`.

A local record is evidence, not automatic release qualification. Release evidence must use the exact packaged artifacts
and pass the gates in [Releasing](../../deployment/releasing.md).

## Evidence Index

The verdicts below are the original recorded outcomes, not new qualifications. Historical runners and report formats
differ from the current plan; missing measurements must not be inferred from a `PASS`. Performance-only runs do not
establish functional or recovery coverage, and requested baud is not independently measured wire speed.

| Record | Recorded Result | Scope And Limitations |
| --- | --- | --- |
| [2026-10-08 Pico 2](2026-10-08-173102Z-pico2-hil.md) | `PARTIAL` | Performance only, above the current default sweep, at low achieved load. Its historical “supported rate” label is not a general baud-capability claim. |
| [2026-10-08 Pico](2026-10-08-183203Z-pico-hil.md) | `FAIL` | Missing stream results and incomplete performance evidence; functional success does not override the overall failure. |
| [2026-10-09 Pico](2026-10-09-070652Z-pico-hil.md) | `FAIL` | Failed concurrent sweep with final RX errors. Later rates followed failures, and per-rate health was not recorded. |
| [2026-10-09 Pico 250 MHz](2026-10-09-071809Z-pico-250mhz-hil.md) | `FAIL` | Overclock artifact with documented provenance; failed rates, missing recovery/soak coverage, and no per-rate health qualification. |
| [2026-10-09 Pico 2](2026-10-09-072834Z-pico2-hil.md) | `PASS` | One functional/concurrent sweep at USB-paced load, without per-rate health or the complete recovery/soak gates. |
| [2026-10-09 Pico 2 280 MHz](2026-10-09-074700Z-pico2-280mhz-hil.md) | `PASS` | Local overclock build, not the packaged release artifact; one sweep does not establish general overclock stability. |
| [2026-10-10 Pico 2 four-hour soak](2026-10-10-013150Z-pico2-921600-4h.md) | `PASS` | One configured rate under USB-paced load; physical flash byte identity was not independently established in the record. |
| [2026-10-10 Pico 2 twelve-hour soak](2026-10-10-173721Z-pico2-921600-12h.md) | `PASS` | One configured rate under USB-paced load; not continuous wire-capacity or full release-matrix qualification. |

Health summaries describe the observations actually retained; do not infer continuous error sampling from before/after
checks. Paths to artifacts or transcripts in a tester's local cache are provenance, not repository downloads. Preserve
the referenced files with their hashes in an accessible evidence archive when sharing or qualifying a release.
Do not backfill unknown provenance, reinterpret failed rows as passes, or overwrite historical results with later runs.
