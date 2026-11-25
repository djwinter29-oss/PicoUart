# Testing PicoUart

PicoUart is tested at five levels. Start with the smallest level that covers your change; record partial hardware runs
as partial.

| Level | Environment | Purpose |
| --- | --- | --- |
| Host tests | Native host | Firmware policies, host applications, and cross-project contracts |
| Firmware build | Pico SDK/toolchain | Compile and link both RP2040 and RP2350 targets |
| Functional HIL | Board + fixed four-link fixture | Enumeration, bidirectional bytes, backend mapping, HID health |
| Performance HIL | Same fixture | Payload integrity under concurrent six-port and per-pair load |
| Release HIL | Exact packaged artifacts | Qualify the artifacts intended for publication |

## Hardware Authorization

An attached board is not authorization. Get explicit user approval before flashing, resetting, changing board or UART
configuration, sending board-control commands, or transmitting UART data. Identify the target and stay within the
approved scope.

## Without Hardware

After the [Python environment setup](../development/host-tools.md#python-environment-setup), run this from the
repository root:

```sh
PYTHON_EXE=host/python/.venv/bin/python tools/validation/run-host-tests.sh
for board in pico pico2; do
  tools/firmware/build.sh --board "$board"
  python3 tools/release/check-firmware-budget.py --build-dir "build/firmware-$board" --board "$board"
done
git diff --check
```

The host-test wrapper runs native C and Python tests; add `--skip-python` to run only the C suite. It does not cover
the .NET host or browser apps. If you changed the .NET host, run the
[.NET checks](../development/host-tools.md#net-development). For browser changes, run the
[frontend tests](../development/dashboard-development.md#frontend-tests). Hardware-free checks cannot establish DMA
timing, USB enumeration, multicore load, or physical UART signalling.

## With Hardware

Follow these steps in order; each document below owns one step.

1. Wire the fixture: [HIL Fixture Setup](hil-fixture-setup.md).
2. Flash the image and record its version and SHA-256 ([USB](../hardware-setup.md#installing-firmware) or
   [SWD](../../firmware/build-and-config.md#debug-probe-loading-and-recovery)). Confirm CDC0 through CDC5 and HID
   enumerate.
3. Run the [standard combined run](hil-fixture-test-plan.md#standard-combined-run). It writes a dated record to
   [records/](records/README.md). The same plan has the pair sweeps.
4. Run the [recovery, stress, and startup checks](hil-recovery-and-stress.md) the change or release requires. Open
   coverage is under [Known Gaps](hil-recovery-and-stress.md#known-gaps).
5. For a release, test the exact packaged artifacts and follow the gates in
   [Releasing](../deployment/releasing.md#release-hil-gates).
6. Remove the fixture jumpers before connecting external UART equipment.

For a quick check after a change, run the concurrent fixture for 60 seconds at 115200 baud, then test individual links
at 921600 and 1000000. Capture HID health before and after, while no other monitor is reading the board:

```sh
host/python/.venv/bin/pico-uart monitor --duration 5
host/python/.venv/bin/pico-uart overruns
```

This is not release stability evidence; see the [evidence index](records/README.md#evidence-index) for earlier long
runs.

## Results and Evidence

| Result | Meaning |
| --- | --- |
| `PASS` | Every required case for the declared level passed. |
| `FAIL` | A required case failed, or there was unexplained loss or errors. |
| `PARTIAL` | A case, link, board, or artifact was intentionally left out. It is useful evidence, but it cannot qualify the design or a release. |
| `NOT RUN` | A configured rate was never attempted, for example after a sweep stopped. |
| `NOT REPORTED` | An expected per-stream result is missing. |

Use the [HIL records guide](records/README.md#record-formats) for required record contents and evidence format.
