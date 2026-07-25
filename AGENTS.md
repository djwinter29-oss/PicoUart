# AGENTS.md

## Cursor Cloud specific instructions

PicoUart is embedded firmware (no long-running servers or databases). The "application"
is the RP2040/RP2350 firmware, and end-to-end product validation requires **physical
hardware** (a Pico board + Raspberry Pi Debug Probe + jumper wires) that is **not present
in the cloud VM**. What can be done here is building the firmware and running the host
tooling.

### Environment provisioned in the VM snapshot (do not re-add to the update script)

These are installed once and captured in the snapshot:

- `ninja-build`, `gcc-arm-none-eabi` + `libstdc++-arm-none-eabi-*` (cross toolchain), `gcovr` (apt)
- `libstdc++-14-dev` (apt) — **required gotcha**: the default `/usr/bin/c++` is clang, which
  selects the gcc-14 toolchain dir. Without `libstdc++-14-dev` the **native host-tool build
  (picotool) fails with `cannot find -lstdc++`** even though `libstdc++-13-dev` is present.
- Pico SDK 2.2.0 with submodules, cloned into the gitignored `.pico-sdk/` by
  `tools/linux/setup-sdk-env.sh`. Persisted in the snapshot; the setup script is idempotent
  (skips the clone if `.pico-sdk/` already exists).

The update script only refreshes the Python host dependency (`host/python/requirements.txt`).

### Build / test / run (standard commands live in the scripts; see `.github/skills/pico-uart-board-testing/SKILL.md`)

- Build firmware: `tools/linux/build.sh --board pico` (RP2040) and `--board pico2` (RP2350).
  Artifacts land in `build/firmware-<board>/pico_uart.{elf,uf2,bin,hex}`.
- If you build in a shell that hasn't sourced `setup-sdk-env.sh`, `build.sh` still finds
  `.pico-sdk/` automatically (defaults `PICO_SDK_PATH` to `<repo>/.pico-sdk`).
- Tests: `tools/linux/test.sh` — there are currently **no CMake tests**, so it prints
  "No CMake tests are configured for this repository." (`coverage.sh` exits for the same reason).
- Host HID tool: `python3 host/python/pico_uart_hid.py {monitor,temperature,toggle-led,reset}`.
- Serial bridge/stress tests: `tools/linux/serial_bridge_test.py`, `serial_stress_benchmark.py`.

### Expected without hardware

The host tools import and run, but with no board attached the HID tool exits non-zero with
`PicoUart HID interface not found`, and the serial tests have no `/dev/ttyACM*` to target.
This is expected in the cloud VM, not a setup failure. Verifying the firmware builds and that
the built binary contains the `cafe:4010` USB identity + the 6×CDC/HID descriptor is the
best available end-to-end check here.
