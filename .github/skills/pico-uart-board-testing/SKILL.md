---
name: pico-uart-board-testing
description: "Use when: testing, bring-up, flashing, debugging, or validating a PicoUart board, including the four-stage UART fixture, concurrent serial stress benchmarking, and bidirectional UART traffic."
argument-hint: "Describe the board connection or test failure"
user-invocable: true
disable-model-invocation: false
---

# PicoUart Board Testing

This skill is an execution guide. The repository documents are the single
source of truth for wiring, test stages, commands, acceptance criteria, and
results. Do not duplicate pin mappings or connection diagrams here.

## Test Documents

Read the relevant document before testing:

- [`docs/tests/self-test-setup.md`](../../../docs/tests/self-test-setup.md):
  equipment, staged wiring, GPIO assignments, and RTS/CTS.
- [`docs/tests/functional-test-plan.md`](../../../docs/tests/functional-test-plan.md):
  functional sequence and pass criteria.
- [`docs/tests/performance-test-plan.md`](../../../docs/tests/performance-test-plan.md):
  rate matrix, benchmark, soak testing, and acceptance criteria.
- [`docs/tests/performance-test-results.md`](../../../docs/tests/performance-test-results.md):
  concise newest-first result log and template.
- [`docs/releasing.md`](../../../docs/releasing.md): release HIL and artifact-hash gates.

The repository provides runners that implement the documented workflow:

```sh
  python3 tools/run_functional_test.py --help
  python3 tools/run_performance_test.py --help
  python3 tools/run_hardware_test.py --help
```

Use `run_hardware_test.py` for the normal end-to-end run. It runs functional
testing before performance testing and records a concise result entry. Use the
individual runners when diagnosing one phase. Add `--no-record` for a dry run.
Use `run_functional_test.py --stage 1|2|3|4` when jumpers must be changed
between stages; use `--stage all --confirm-rewire` only when the operator can
confirm each rewire interactively.

## Preconditions

Physical hardware is user-provided; CI does not provide the board, Debug Probe,
USB cable, or jumper fixture. Assemble the fixture using
`docs/tests/self-test-setup.md` before continuing.

1. Build the intended board image:

   ```sh
  . tools/setup-sdk-env.sh
  tools/build.sh --board pico
   ```

   Use `--board pico2` for RP2350 hardware.
2. Connect the Debug Probe and target power.
3. Flash the intended ELF with `tools/load.sh`.
4. Connect the Pico USB device and confirm CDC/HID enumeration.
5. Run the functional plan before the performance plan.

For release HIL, flash the exact packaged artifact and follow the SHA256 and
two-board requirements in `docs/releasing.md`.

## OpenOCD Recovery

Use a current OpenOCD CMSIS-DAP build. If OpenOCD prints `cmsis-dap <cmd>`
usage output or `Unknown flash device`, treat it as a flashing-tool issue first.

- Check `openocd --version`.
- Reconnect target power and the Debug Probe.
- Retry at `--adapter-speed-khz 1000`.
- Flash ID `0x00154068` is Boya BY25Q16ES, 2 MiB; older distro OpenOCD builds
  may lack this table entry.
- Select a current upstream binary with `--openocd-exe /path/to/openocd`.

Do not classify an unknown flash ID as a firmware or UART-test failure until
the loader reaches programming and verification.

## Diagnostics

- No USB device: check the flashed image and use a USB data cable.
- No serial access: check `dialout`/`plugdev` permissions and use
  `/dev/serial/by-id` paths.
- Functional failure: return to the staged wiring and functional plan.
- Performance failure: confirm the topology-aware options and settle interval
  in the performance plan, then inspect HID `rx_error`, `control_error`, and
overflow counters.
- Record the outcome in `docs/tests/performance-test-results.md`.
