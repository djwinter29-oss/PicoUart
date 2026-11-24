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

This skill routes board-testing work to the repository documents, which define wiring, commands, acceptance criteria,
release gates, OpenOCD recovery, and result recording. Do not duplicate pin mappings, connection diagrams, or command
matrices here.

## Canonical Documents

Read the relevant document before testing:

- [`README.md`](../../../README.md): documentation index and standard local build/test entry points.
- [`tools/README.md`](../../../tools/README.md): tool categories and local runner entry points.
- [`docs/tests/hil-fixture-setup.md`](../../../docs/tests/hil-fixture-setup.md): equipment, fixed fixture wiring, and
  flow-control policy.
- [`docs/tests/hil-fixture-test-plan.md`](../../../docs/tests/hil-fixture-test-plan.md): functional sequence, rate
  matrix, benchmark, soak testing, and acceptance criteria.
- [`docs/deployment/releasing.md`](../../../docs/deployment/releasing.md): release HIL and artifact-hash gates.

The repository provides runners that implement the documented workflow:

```sh
tools/hil/runner/functional.sh --help
tools/hil/runner/performance.sh --help
tools/hil/runner/full.sh --help
```

Use `tools/hil/runner/full.sh` for the normal end-to-end run. It runs functional testing before performance testing and
records a dated result. Use `tools/hil/runner/functional.sh` or `tools/hil/runner/performance.sh` when diagnosing one
phase. See the [test plan](../../../docs/tests/hil-fixture-test-plan.md#before-you-start) for the effect of
`--no-record`. Install both crossed pairs and both loopbacks before starting and do not change wiring during the run.
Use `tools/hil/runner/functional.sh --stage 1|2|3|4` only to diagnose one link on the same fixed fixture.

Use the [baseline and speed-ceiling rules](../../../docs/tests/hil-fixture-test-plan.md#baseline-and-speed-ceiling)
when running or interpreting a speed sweep.

Before each physical HIL test:

- Flash the selected board with the image under test. For development, use
  `tools/firmware/load.sh --board <pico|pico2>` to build and flash. For release qualification, flash the exact
  packaged artifact with `--skip-build --elf <artifact>` as documented in
  [Firmware Build and Configuration](../../../firmware/build-and-config.md#load-over-swd).
- Verify that the board re-enumerates and reports the expected firmware version. Record the artifact path and SHA-256.
- The combined runner records the live system clock in the report header and filename, for example
  `2026-10-22-171334Z-pico-125mhz-hil.md`. If it says `clock-unknown`, run `pico-uart hardware` and record the
  reported clock; if that query fails, leave the clock unknown. Do not infer it from the board model or profile.
- If OpenOCD cannot identify QSPI flash, follow the verified-capacity procedure in
  [Firmware Build and Configuration](../../../firmware/build-and-config.md#qspi-auto-detection-override).

## Diagnostics

- In a VM without a board or fixture, run build and host-tool checks; they do not replace physical HIL.
- For physical failures, return to the canonical setup/test/release docs above.
- Record and retain results as described in the [HIL records guide](../../../docs/tests/records/README.md); keep release
  evidence with the exact artifacts.
