---
name: pico-uart-board-testing
description:
  "Use when: testing, bring-up, flashing, debugging, or validating a PicoUart board, including the four-stage UART
  fixture, concurrent serial stress benchmarking, and bidirectional UART traffic."
argument-hint: "Describe the board connection or test failure"
user-invocable: true
disable-model-invocation: false
---

# PicoUart Board Testing

This skill routes board-testing work to the repository documents. Those docs are the single source of truth for wiring,
commands, acceptance criteria, release gates, OpenOCD recovery, and result recording. Do not duplicate pin mappings,
connection diagrams, or command matrices here.

## Canonical Documents

Read the relevant document before testing:

- [`README.md`](../../../README.md): documentation index and standard local build/test entry points.
- [`tools/README.md`](../../../tools/README.md): tool categories and local runner entry points.
- [`docs/tests/hil-fixture-setup.md`](../../../docs/tests/hil-fixture-setup.md): equipment, fixed fixture wiring, and
  flow-control policy.
- [`docs/tests/hil-fixture-test-plan.md`](../../../docs/tests/hil-fixture-test-plan.md): functional sequence, rate
  matrix, benchmark, soak testing, and acceptance criteria.
- [`docs/releasing.md`](../../../docs/releasing.md): release HIL and artifact-hash gates.

The repository provides runners that implement the documented workflow:

```sh
  tools/hil/runner/functional.sh --help
  tools/hil/runner/performance.sh --help
  tools/hil/runner/full.sh --help
```

Use `tools/hil/runner/full.sh` for the normal end-to-end run. It runs functional testing before performance testing and
records a dated result. Use `tools/hil/runner/functional.sh` or `tools/hil/runner/performance.sh` when diagnosing one
phase. Add `--no-record` for a dry run. Install both crossed pairs and both loopbacks before starting and do not change
wiring during the run. Use `tools/hil/runner/functional.sh --stage 1|2|3|4` only to diagnose one link on the same fixed
fixture.

Before every physical HIL test, flash the selected board with the firmware being tested; never assume the board is
already running the intended image. For development tests, use `tools/firmware/load.sh --board <pico|pico2>` to build
and flash. For release qualification, flash the exact packaged artifact with `--skip-build --elf <artifact>` as
documented in [Releasing](../../../docs/releasing.md#flashing-release-artifacts). After flashing, verify the board
re-enumerates and reports the expected firmware version, then record the artifact path and SHA-256 in the HIL run.

## Diagnostics

- No hardware in the VM is expected; build and host-tool validation are the best available checks here.
- For physical failures, return to the canonical setup/test/release docs above.
- The combined runner creates a dated record in `docs/tests/records/`; standalone phase runs write to the ignored
  `build/hil-results.md`. Retain release HIL evidence with the exact artifacts.
