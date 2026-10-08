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

Each generated record contains:

- Overall `PASS`, `FAIL`, or `PARTIAL` status
- Board, UTC run time, firmware version, source commit, and artifact SHA-256 when supplied
- Functional stage outcomes and performance outcome
- The exact child commands and captured output
- HIL health snapshots and any caveats

A local record is evidence, not automatic release qualification. Release evidence must use the exact packaged artifacts
and pass the gates in [Releasing](../../releasing.md).
