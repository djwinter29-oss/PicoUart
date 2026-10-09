# AGENTS.md

## Cursor Cloud specific instructions

PicoUart is embedded firmware. The "application" is the RP2040/RP2350 firmware, not a server or database. Use
[README.md](README.md) as the documentation index; do not duplicate build, test, wiring, release, or USB-interface
details here.

End-to-end validation requires physical hardware (Pico/Pico 2, Raspberry Pi Debug Probe, USB cable, and jumper-wire
fixture) that is not present in the cloud VM. In this environment, verify firmware builds, host tooling, host tests, and
binary USB contracts only; see [Hardware-in-the-loop testing](#hardware-in-the-loop-testing) for what to defer to a
local run.

### Environment provisioned in the VM snapshot (do not re-add to the update script)

These are installed once and captured in the snapshot. Do not apt-install them from the update script:

- `ninja-build`, `gcc-arm-none-eabi`, `libstdc++-arm-none-eabi-newlib`, `libnewlib-arm-none-eabi`, `gcovr`, and
  `cppcheck` (apt)
- `libstdc++-14-dev` (apt) — **required gotcha**: the default `/usr/bin/c++` is clang, which selects the gcc-14
  toolchain dir. Without `libstdc++-14-dev` the **native host-tool build (picotool) fails with `cannot find -lstdc++`**
  even though `libstdc++-13-dev` is present.
- `python3-venv`, `python3-dev`, `libusb-1.0-0-dev`, `libhidapi-dev`, and `libhidapi-hidraw0` (apt), so the host venv
  and HID library can be created.

### Update script

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

### Build / test / run

Canonical commands live in [README.md](README.md); tool categories and command mappings live in
[tools/README.md](tools/README.md). There is no boot `start` command — the diagnostics dashboard is `pico-uart web` and
is started only when someone is using it.

### Expected without hardware

The host tools import and run, but with no board attached the HID tool exits non-zero with
`PicoUart HID interface not found`, and the serial tests have no `/dev/ttyACM*` to target. This is expected in the cloud
VM, not a setup failure. Verifying the firmware builds and that the built binary contains the `cafe:4010` USB identity +
the 6×CDC/HID descriptor is the best available end-to-end check here.

### Hardware-in-the-loop testing

Physical HIL validation needs hardware this VM does not have. Do not re-derive fixture, dated-record, or local-log
conventions here; see [docs/tests/README.md](docs/tests/README.md) for the HIL plan and record layout and
[docs/releasing.md](docs/releasing.md#release-hil-gates) for release qualification gates.

Firmware development, flashing, and HIL use the Linux wrappers documented in [tools/README.md](tools/README.md). Native
Windows firmware-development wrappers are not supported; Windows users should use WSL2 Ubuntu. The Python host tools
remain usable from Windows for CDC/HID operation.

### Non-obvious firmware caveats

- CDC/HID ownership and report contracts: [docs/design/usb/cdc-hid-overview.md](docs/design/usb/cdc-hid-overview.md) and
  [docs/design/usb/hid-report-reference.md](docs/design/usb/hid-report-reference.md).
- Control-plane lifecycle and `CONTROL_ERROR` / `CONTROL_PENDING` behavior:
  [docs/design/control-plane-design.md](docs/design/control-plane-design.md).
- Release and OpenOCD/HIL gates: [docs/releasing.md](docs/releasing.md).
