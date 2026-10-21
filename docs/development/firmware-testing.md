# Firmware and Repository Testing

This guide summarizes development boundaries and test levels. Detailed build and load steps are in
[Firmware Build and Configuration](../../firmware/build-and-config.md); canonical test commands and HIL procedures are
in the [Test Documentation Index](../tests/README.md) and [Repository Tools](../../tools/README.md).

## Design Boundaries

Keep board pin mapping separate from USB and UART transport logic. See [Architecture](../design/architecture.md), the
[UART subsystem design](../design/uart/README.md), [Hardware Setup](../hardware-setup.md), and the
[CDC/HID overview](../design/usb/cdc-hid-overview.md) for their respective responsibilities and constraints.

## Test Scope

Validation is split across native firmware logic tests, host and cross-project contract tests, and physical HIL. The
root [pytest configuration](../../pyproject.toml) discovers repository tests; host packages and the HIL runner also have
project-specific configuration. Use the test index to choose the applicable level and command rather than treating one
suite as a substitute for another.

## CI And Hardware Scope

CI validates host behavior and firmware builds. It has no physical board or wiring fixture and cannot prove UART
signaling, USB enumeration, DMA timing, or multicore behavior under load. Physical acceptance and exact-artifact release
gates are defined in the [hardware test documentation](../tests/README.md) and
[release guide](../deployment/releasing.md).

## Cursor Cloud Environment

This section describes the provisioned Cursor Cloud VM snapshot, not requirements or hardware availability for every
local checkout. The snapshot has no attached board, debug probe, or UART fixture; use the non-hardware checks above.

### Snapshot Packages

These packages are installed once and captured in the snapshot. Do not apt-install them from the update script:

- `ninja-build`, `gcc-arm-none-eabi`, `libstdc++-arm-none-eabi-newlib`, `libnewlib-arm-none-eabi`, `gcovr`, and
	`cppcheck` (apt).
- `libstdc++-14-dev` (apt): the snapshot's default `/usr/bin/c++` is clang and selects the gcc-14 toolchain directory.
	Without it, the native picotool build fails with `cannot find -lstdc++`, even with `libstdc++-13-dev` present.
- `python3-venv`, `python3-dev`, `libusb-1.0-0-dev`, `libhidapi-dev`, and `libhidapi-hidraw0` (apt) for the host venv
	and HID library.

### Update Script

Run this update script in Bash; it refreshes the checkout environment and does not start a service:

```bash
set -euo pipefail
python3 -m venv host/python/.venv
host/python/.venv/bin/python -m pip install --require-hashes -r host/python/requirements-lock.txt
host/python/.venv/bin/python -m pip install --no-deps --no-build-isolation -e host/python
. tools/firmware/setup-sdk-env.sh --sdk-version 2.3.0 --sdk-revision 98a542c1a62fb549ffb5d66a3e5892b06276b670
```

The Python commands install locked development dependencies and `pico-uart` editable; see the
[host developer setup](host-tools.md#python-environment-setup).
`setup-sdk-env.sh` clones the pinned SDK into gitignored `.pico-sdk/` if missing, and otherwise confirms the pin.
Keep the SDK line: a fresh checkout does not carry `.pico-sdk/`.

Ubuntu's `/usr/bin/python3` is PEP 668 externally managed. Point the host-test wrapper at the venv rather than its
default system interpreter:

```sh
PYTHON_EXE=host/python/.venv/bin/python tools/validation/run-host-tests.sh
host/python/.venv/bin/python -m pico_uart --help
```
