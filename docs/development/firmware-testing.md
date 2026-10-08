# Firmware and Repository Testing

This guide maps the firmware development surface and repository-wide automated tests. Detailed firmware build, load, and
configuration steps remain in [Firmware Build and Configuration](../../firmware/build-and-config.md).

## Firmware Source Map

- `firmware/src/board/`: board-specific GPIO and peripheral mapping
- `firmware/src/usb/`: TinyUSB CDC/HID configuration and report handling
- `firmware/src/uart/`: UART facade, HW/PIO backends, worker, control plane, and rings
- `firmware/tests/`: native C tests for firmware logic
- `tests/firmware/`: repository-level firmware policy and PIO behavior tests

Keep board pin mapping separate from USB and UART transport logic. System architecture and data ownership are documented
in [Architecture](../architecture.md); board wiring is in [UART Pinout](../uart-pinout.md), and USB behavior is in the
[CDC/HID overview](../usb/cdc-hid-overview.md).

Firmware constraints to account for during development:

- Six CDC interfaces map one-to-one to six UART channels.
- CDC carries UART data and line coding; HID reports status and limited board controls.
- The two hardware UART ports support the wider CDC line-coding set; PIO ports are 8N1-only.
- Flow control and remote HID reset are disabled by default.
- Trusted lab builds can enable HID reset with `-DPICO_UART_ALLOW_HID_RESET=1`; do not enable it for untrusted hosts.
- Firmware startup preserves the regulator voltage present on entry; clock overrides do not set core voltage.

## Test Layout

The root [pyproject.toml](../../pyproject.toml) configures repository-wide pytest discovery. The package-specific
[host Python pyproject](../../host/python/pyproject.toml) keeps its test scope for package-only work. The reusable
hardware-in-the-loop tools have a separate [Python project](../../tools/hil/pyproject.toml) and test suite.

- `host/python/tests/`: host package tests, fixtures, and HID report helpers
- `tools/hil/src/hil_test_suite/workflows/`: full, functional, and performance orchestration
- `tools/hil/src/hil_test_suite/serial/`: bridge, stress, pair, and serial-port configuration
- `tools/hil/src/hil_test_suite/support/`: repository paths, HID health, and result-record helpers
- `tools/hil/tests/`: unit tests for HIL utilities and CLI behavior
- `tests/firmware/`: firmware policy and PIO tests
- `tests/tooling/`: repository validation scripts and cross-project contract tests
- `tests/contracts/`: firmware/host, CI, and release contract tests
- `firmware/tests/`: native C tests using Unity, CMake, and CTest

## Validation Commands

Run the complete host-side C and Python suite from the repository root:

```sh
tools/validation/run-host-tests.sh
```

Run the reusable HIL tool tests in their isolated environment with:

```sh
uv sync --frozen --project tools/hil --extra test
uv run --frozen --project tools/hil --extra test pytest tools/hil/tests
```

Run the combined firmware build and host validation flow with `tools/validation/validate.sh`. Its `--skip-build` and
`--skip-host` options select which half to omit. To run just the Python project tests:

```sh
host/python/.venv/bin/python -m pytest -c pyproject.toml
```

Run only package tests from `host/python` with:

```sh
.venv/bin/python -m pytest -c pyproject.toml
```

The validation scripts in `tools/validation/` also provide syntax checks, static analysis, smoke checks, and firmware C
coverage. The host suite does not require a board; physical UART, USB, HID, and throughput qualification are covered by
the [hardware test documentation](../tests/README.md).

## CI and Hardware Scope

CI runs host tests and builds rated Pico/Pico 2 firmware targets. PR and release workflows also build development
overclock images; the application does not write or restore a core voltage. CI has no physical board or wiring fixture,
so it does not replace hardware qualification. See the [release guide](../releasing.md) for exact-artifact gates and the
[Test Documentation Index](../tests/README.md) for the hardware test sequence.
