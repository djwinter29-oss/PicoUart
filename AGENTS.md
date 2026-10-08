# AGENTS.md

## Cursor Cloud specific instructions

PicoUart is embedded firmware. The "application" is the RP2040/RP2350 firmware, not a server or database. Use
[README.md](README.md) as the documentation index; do not duplicate build, test, wiring, release, or USB-interface
details here.

End-to-end validation requires physical hardware (Pico/Pico 2, Raspberry Pi Debug Probe, USB cable, and jumper-wire
fixture) that is not present in the cloud VM. In this environment, verify firmware builds, host tooling, host tests, and
binary USB contracts only.

### Environment provisioned in the VM snapshot (do not re-add to the update script)

These are installed once and captured in the snapshot. Do not apt-install them from the update script:

- `ninja-build`, `gcc-arm-none-eabi`, `libstdc++-arm-none-eabi-newlib`, `libnewlib-arm-none-eabi`, `gcovr`, and
  `cppcheck` (apt)
- `libstdc++-14-dev` (apt) — **required gotcha**: the default `/usr/bin/c++` is clang, which selects the gcc-14
  toolchain dir. Without `libstdc++-14-dev` the **native host-tool build (picotool) fails with `cannot find -lstdc++`**
  even though `libstdc++-13-dev` is present.
- `python3-venv`, `python3-dev`, `libusb-1.0-0-dev`, `libhidapi-dev`, and `libhidapi-hidraw0` (apt), so the host venv
  and HID library can be created.

The update script refreshes the host checkout. It does not start a service:

```sh
set -euo pipefail
tools/host/setup-venv.sh
. tools/firmware/setup-sdk-env.sh --sdk-version 2.3.0 --sdk-revision 98a542c1a62fb549ffb5d66a3e5892b06276b670
```

`tools/host/setup-venv.sh` installs `host/python/requirements-lock.txt` into `host/python/.venv` and installs
`pico-uart` editable. `setup-sdk-env.sh` is idempotent: it clones Pico SDK 2.3.0
(`98a542c1a62fb549ffb5d66a3e5892b06276b670`) into gitignored `.pico-sdk/` when that checkout is missing, and otherwise
confirms the pin. Keep the SDK line in the update script. A fresh checkout does not carry `.pico-sdk/`.

Ubuntu's `/usr/bin/python3` is PEP 668 externally managed. `tools/validation/run-host-tests.sh` defaults to that
interpreter and then fails its pip install. Point it at the venv:

```sh
PYTHON_EXE=host/python/.venv/bin/python tools/validation/run-host-tests.sh
host/python/.venv/bin/python -m pico_uart --help
```

There is no boot `start` command. The diagnostics dashboard is `pico-uart web` and is started only when someone is
using it.

Physical hardware testing is not available in CI. Users must provide the Pico/Pico 2 board, USB data cable, and
jumper-wire fixture and run the documented HIL plan locally. The Debug Probe UART is optional for manual external-peer
checks and is not part of the canonical HIL fixture. The combined runner creates a dated record in
`docs/tests/records/`; standalone phase runs go to the ignored `build/hil-results.md`. Retain release evidence with the
exact artifacts.

Firmware development, flashing, and HIL use the Linux wrappers documented in [tools/README.md](tools/README.md). Native
Windows firmware-development wrappers are not supported; Windows users should use WSL2 Ubuntu. The Python host tools
remain usable from Windows for CDC/HID operation.

### Build / test / run

Canonical commands live in [README.md](README.md). Hardware/HIL workflow details live in [docs/tests](docs/tests) and
[docs/releasing.md](docs/releasing.md).

### Expected without hardware

The host tools import and run, but with no board attached the HID tool exits non-zero with
`PicoUart HID interface not found`, and the serial tests have no `/dev/ttyACM*` to target. This is expected in the cloud
VM, not a setup failure. Verifying the firmware builds and that the built binary contains the `cafe:4010` USB identity +
the 6×CDC/HID descriptor is the best available end-to-end check here.

### Non-obvious firmware caveats

- CDC/HID ownership and report contracts: [docs/usb/cdc-hid-overview.md](docs/usb/cdc-hid-overview.md) and
  [docs/usb/hid-report-reference.md](docs/usb/hid-report-reference.md).
- Control-plane lifecycle and `CONTROL_ERROR` / `CONTROL_PENDING` behavior:
  [docs/detail/control-plane-design.md](docs/detail/control-plane-design.md).
- Release and OpenOCD/HIL gates: [docs/releasing.md](docs/releasing.md).
