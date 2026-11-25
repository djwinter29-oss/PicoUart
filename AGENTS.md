# AGENTS.md

## Environment and Validation

Check the current environment before assuming tools or hardware are available. Choose checks relevant to the change;
these tiers describe available validation, not a mandatory checklist for every task:

- No board: verify firmware builds, host tooling, host tests, and binary USB contracts.
- Board only: also check USB enumeration and read-only HID status on the identified PicoUart board. These checks do not
  require a UART fixture and do not establish physical UART behavior.
- Board with the required fixture: run the applicable HIL procedures within the user's authorized scope. Hardware
  qualification applies only to the tested artifacts and recorded conditions.

For hardware actions, require explicit user authorization, identify the target, and stay within the approved scope.
Follow the human-facing [hardware authorization rules](docs/tests/README.md#hardware-authorization).

With no compatible board attached, a HID command may exit non-zero with `PicoUart HID interface not found`; this is
expected, not a setup failure. Unrelated serial devices may still be present, so do not assume any `/dev/ttyACM*`
belongs to PicoUart.

Use the [host developer setup](docs/development/host-tools.md#python-environment-setup) for Python dependencies and
the [Cursor Cloud guide](docs/development/cursor-cloud.md) when working in that environment.

## Build, Test, and Run

Use [docs/tests/README.md](docs/tests/README.md) to choose validation scope and [tools/README.md](tools/README.md) for
command mappings. Start dashboards or test servers only when requested or for bounded validation, and stop temporary
services when the work is done.
