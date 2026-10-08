# HIL Records

The `pico-uart-hil` command creates one Markdown record per invocation in this directory. Use `--no-record` to suppress
generation or `--record-dir` to choose another output directory.

## Naming

Names use UTC date/time, board target, and test kind:

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
- A compact final HID health summary

Reports intentionally omit child command lines and verbose monitor transcripts. The terminal still shows live test output
while a run is in progress. Standalone phase entries are appended to the ignored local log at `build/hil-results.md`.

A local record is evidence, not automatic release qualification. Release evidence must use the exact packaged artifacts
and pass the gates in [Releasing](../../releasing.md).
