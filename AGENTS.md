# AGENTS.md

## Repository Guidance

PicoUart is embedded firmware with companion host tools, not a server or database application. Use
[README.md](README.md) as the documentation index; do not duplicate build, test, wiring, release, or USB-interface
details here. Follow the shared coding and documentation conventions in
[.github/copilot-instructions.md](.github/copilot-instructions.md).

## Environment And Validation

Check the current environment before assuming tools or hardware are available. Choose validation scope accordingly:

- No board: verify firmware builds, host tooling, host tests, and binary USB contracts.
- Board only: also check USB enumeration and read-only HID status on the identified PicoUart board. These checks do not
  require a UART fixture and do not establish physical UART behavior.
- Board with the required fixture: run the applicable HIL procedures within the user's authorized scope. Hardware
  qualification applies only to the tested artifacts and recorded conditions.

An attached board is not authorization to modify it or exercise connected devices. Require explicit user authorization
before flashing, resetting, changing board or UART configuration, sending board-control commands, or transmitting UART
data. Identify the target and stay within the authorized scope; do not perform these actions merely to validate docs or
UI changes.

With no compatible board attached, a HID command may exit non-zero with `PicoUart HID interface not found`; this is
expected, not a setup failure. Unrelated serial devices may still be present, so do not assume any `/dev/ttyACM*`
belongs to PicoUart.

Use the [host developer setup](docs/development/host-tools.md#python-environment-setup) for Python dependencies and
[firmware development guide](docs/development/firmware-testing.md#cursor-cloud-environment) for Cursor Cloud snapshot
provisioning and its update script. Those snapshot assumptions do not apply to every checkout.

## Build, Test, And Run

Use [tools/README.md](tools/README.md) for command mappings and [docs/tests/README.md](docs/tests/README.md) to choose
validation scope. There is no boot `start` command; the diagnostics dashboard is `pico-uart web` and is started only
when someone is using it.

Firmware development, flashing, and HIL use the Linux wrappers documented in [tools/README.md](tools/README.md). Native
Windows firmware-development wrappers are not supported; Windows users should use WSL2 Ubuntu. The Python host tools
remain usable from Windows for CDC/HID operation.

## Hardware And Firmware Contracts

- CDC/HID ownership and report contracts: [docs/design/usb/cdc-hid-overview.md](docs/design/usb/cdc-hid-overview.md) and
  [docs/design/usb/hid-report-reference.md](docs/design/usb/hid-report-reference.md).
- Control-plane lifecycle and `CONTROL_ERROR` / `CONTROL_PENDING` behavior:
  [docs/design/control-plane-design.md](docs/design/control-plane-design.md).
- Fixture, dated-record, and local-log conventions: [docs/tests/README.md](docs/tests/README.md).
- Release qualification and OpenOCD/HIL gates: [docs/releasing.md](docs/releasing.md#release-hil-gates).
