---
name: pico-uart-board-testing
description: "Use when: testing, bring-up, flashing, debugging, or validating a PicoUart board, including the four-stage UART fixture, concurrent serial stress benchmarking, and bidirectional UART traffic."
argument-hint: "Describe the board connection or test failure"
user-invocable: true
disable-model-invocation: false
---

# PicoUart Board Testing

This skill routes board-testing work to the repository documents. Those docs are
the single source of truth for wiring, commands, acceptance criteria, release
gates, OpenOCD recovery, and result recording. Do not duplicate pin mappings,
connection diagrams, or command matrices here.

## Canonical Documents

Read the relevant document before testing:

- [`README.md`](../../../README.md): documentation index and standard local
  build/test entry points.
- [`docs/tests/self-test-setup.md`](../../../docs/tests/self-test-setup.md):
  equipment, fixed fixture wiring, and flow-control policy.
- [`docs/tests/functional-test-plan.md`](../../../docs/tests/functional-test-plan.md):
  functional sequence and pass criteria.
- [`docs/tests/performance-test-plan.md`](../../../docs/tests/performance-test-plan.md):
  rate matrix, benchmark, soak testing, and acceptance criteria.
- [`docs/tests/performance-test-results.md`](../../../docs/tests/performance-test-results.md):
  concise newest-first result log and template.
- [`docs/releasing.md`](../../../docs/releasing.md): release HIL and artifact-hash gates.

The repository provides runners that implement the documented workflow:

```sh
  python3 tools/hardware/run_functional_test.py --help
  python3 tools/hardware/run_performance_test.py --help
  python3 tools/hardware/run_hardware_test.py --help
```

Use `tools/hardware/run_hardware_test.py` for the normal end-to-end run. It runs functional
testing before performance testing and records a concise result entry. Use the
individual runners when diagnosing one phase. Add `--no-record` for a dry run.
Install all four independent links before starting and do not change wiring
during the run. Use `tools/hardware/run_functional_test.py --stage 1|2|3|4` only to diagnose
one link on the same fixed fixture.

## Diagnostics

- No hardware in the VM is expected; build and host-tool validation are the best
  available checks here.
- For physical failures, return to the canonical setup/test/release docs above.
- Record physical outcomes in `docs/tests/performance-test-results.md`.
