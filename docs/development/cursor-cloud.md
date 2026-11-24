# Cursor Cloud Development Environment

This guide covers the provisioned Cursor Cloud VM snapshot. It has no attached board, debug probe, or UART fixture;
use the [hardware-free test checklist](../tests/README.md#without-hardware).

## Snapshot Packages

These packages are installed once and captured in the snapshot. Do not apt-install them from the update script:

- `ninja-build`, `gcc-arm-none-eabi`, `libstdc++-arm-none-eabi-newlib`, `libnewlib-arm-none-eabi`, `gcovr`, and
  `cppcheck` (apt).
- `libstdc++-14-dev` (apt): the snapshot's default `/usr/bin/c++` is clang and selects the gcc-14 toolchain directory.
  Without it, the native picotool build fails with `cannot find -lstdc++`, even with `libstdc++-13-dev` present.
- `python3-venv`, `python3-dev`, `libusb-1.0-0-dev`, `libhidapi-dev`, and `libhidapi-hidraw0` (apt) for the host venv
  and HID library.

## Update Script

Run this update script in Bash; it refreshes the checkout environment and does not start a service:

```sh
set -euo pipefail
python3 -m venv host/python/.venv
host/python/.venv/bin/python -m pip install --group host/python/pyproject.toml:dev
host/python/.venv/bin/python -m pip install --no-build-isolation -e host/python
. tools/firmware/setup-sdk-env.sh --sdk-version 2.3.0 --sdk-revision 98a542c1a62fb549ffb5d66a3e5892b06276b670
```

The Python commands install the `dev` dependency group and `pico-uart` editable; see the
[host developer setup](host-tools.md#python-environment-setup).
`setup-sdk-env.sh` clones the pinned SDK into gitignored `.pico-sdk/` if missing, and otherwise confirms the pin.
Keep the SDK line: a fresh checkout does not carry `.pico-sdk/`.

Ubuntu's `/usr/bin/python3` is PEP 668 externally managed. Point the host-test wrapper at the venv rather than its
default system interpreter:

```sh
PYTHON_EXE=host/python/.venv/bin/python tools/validation/run-host-tests.sh
host/python/.venv/bin/python -m pico_uart.main --help
```
