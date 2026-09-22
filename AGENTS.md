# AGENTS.md

## Cursor Cloud specific instructions

PicoUart is embedded firmware. The "application" is the RP2040/RP2350 firmware,
not a server or database. Use [README.md](README.md) as the documentation index;
do not duplicate build, test, wiring, release, or USB-interface details here.

End-to-end validation requires physical hardware (Pico/Pico 2, Raspberry Pi
Debug Probe, USB cable, and jumper-wire fixture) that is not present in the
cloud VM. In this environment, verify firmware builds, host tooling, host tests,
and binary USB contracts only.

### Environment provisioned in the VM snapshot (do not re-add to the update script)

These are installed once and captured in the snapshot:

- `ninja-build`, `gcc-arm-none-eabi` + `libstdc++-arm-none-eabi-*` (cross toolchain), `gcovr` (apt)
- `libstdc++-14-dev` (apt) — **required gotcha**: the default `/usr/bin/c++` is clang, which
  selects the gcc-14 toolchain dir. Without `libstdc++-14-dev` the **native host-tool build
  (picotool) fails with `cannot find -lstdc++`** even though `libstdc++-13-dev` is present.
- Pico SDK 2.3.0 with submodules, cloned into the gitignored `.pico-sdk/` by
  `tools/firmware/setup-sdk-env.sh`. Persisted in the snapshot; the setup script is idempotent
  (skips the clone if `.pico-sdk/` already exists).

The update script refreshes Python host dependencies (`requirements.txt` and
`requirements-dev.txt` when present).

Physical hardware testing is not available in CI. Users must provide the
Pico/Pico 2 board, Raspberry Pi Debug Probe, USB cable, and jumper-wire fixture
and run the documented functional/performance plans locally. Record results in
`docs/tests/performance-test-results.md`.

Firmware development, flashing, and HIL use the Linux wrappers under
`tools/`. Native Windows firmware-development wrappers are not supported;
Windows users should use WSL2 Ubuntu. The Python host tools remain usable from
Windows for CDC/HID operation.

### Build / test / run

Canonical commands live in [README.md](README.md). Hardware/HIL workflow details
live in [docs/tests](docs/tests) and [docs/releasing.md](docs/releasing.md).

### Expected without hardware

The host tools import and run, but with no board attached the HID tool exits non-zero with
`PicoUart HID interface not found`, and the serial tests have no `/dev/ttyACM*` to target.
This is expected in the cloud VM, not a setup failure. Verifying the firmware builds and that
the built binary contains the `cafe:4010` USB identity + the 6×CDC/HID descriptor is the
best available end-to-end check here.

### Non-obvious firmware caveats

- CDC/HID ownership and report contracts: [docs/usb/cdc-hid-overview.md](docs/usb/cdc-hid-overview.md)
  and [docs/usb/hid-report-reference.md](docs/usb/hid-report-reference.md).
- Control-plane lifecycle and `CONTROL_ERROR` / `CONTROL_PENDING` behavior:
  [docs/detail/control-plane-design.md](docs/detail/control-plane-design.md).
- Release and OpenOCD/HIL gates: [docs/releasing.md](docs/releasing.md).
