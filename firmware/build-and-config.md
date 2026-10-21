# Firmware Build and Configuration

This folder contains the Pico SDK firmware project `pico_uart` for PicoUart.

Use the root [README](../README.md) as the documentation index. This file keeps only firmware-local build, load, and
configuration notes.

## Build

On Ubuntu/Linux, install CMake, Ninja, an ARM GCC toolchain, and use the project-local Pico SDK:

```sh
. tools/firmware/setup-sdk-env.sh
tools/firmware/build.sh
tools/firmware/build.sh --board pico2
```

With no arguments or `PICO_BOARD` override, the build defaults to `pico` (RP2040), writing to `build/firmware-pico`.
Use `--board pico2` for RP2350; an explicit `--board` takes precedence over `PICO_BOARD`.

Optional: stamp a release version into the firmware with `--firmware-version` (for example `1.2.3` from tag `v1.2.3`).
Release version policy is documented in [Releasing](../docs/releasing.md).

All generated output is stored under the repository-root `build/` directory. Use a separate build directory per board.
The Linux build/load tools accept `--board` values supported by the installed Pico SDK.

The scripts honor an exported `PICO_SDK_PATH`; otherwise they use the pinned repository-local `.pico-sdk` checkout.
`load.sh` uses the same SDK selection as `build.sh` when it rebuilds an image.

Clock profile selection moved to persistent board configuration in the final flash sector. Build-time clock overrides
are intentionally rejected so the same firmware image can boot with SDK defaults or a stored overclock profile.
After flashing, use the dashboards' [Persistent configuration](../docs/development/host-tools.md#persistent-configuration)
section to select and save a profile over HID, then reboot manually. SDK default does not reconfigure the clock.
The [SWD configuration tool](../tools/firmware/config.py) remains available to read, set, or erase the profile,
including recovery when USB is unavailable.
Any invalid configuration record reverts to SDK/board defaults.

Firmware writes no core-voltage registers for default or non-default profiles; the regulator setting on entry is
preserved. The compile/link voltage-policy test checks this behavior against the real SDK startup object and ELF:

Each firmware matrix leg in PR check CI runs this check automatically, right after `verify-build.py`, against its own
build directory. After building locally, run the same check yourself to inspect the real SDK startup object, resolved
preprocessor policy, and linked ELF (not just the mock application test):

```sh
PICO_UART_VOLTAGE_BUILD_DIRS="$PWD/build/pico:$PWD/build/pico2" \
  python3 -m pytest -c pyproject.toml \
  tests/firmware/test_system_clock_voltage_policy.py -k real_sdk -o addopts='' -q
```

Use your completed build directories. The check detects SDK automatic voltage adjustment and regulator writes in the
startup object/ELF; it does not measure physical voltage, thermal margin, or hardware stability.

Changing board, SDK path, generator, firmware version, or HID-reset option causes the build wrapper to reset stale
CMake cache state before reconfiguring. Dependency build caches and external-tool stamps are reset together; the
wrapper also detects a leftover picotool configure stamp whose dependency cache is missing.

## Load

The Linux load tool programs the ELF remotely through a Raspberry Pi Debug Probe using CMSIS-DAP OpenOCD. Connect the
probe's SWDIO, SWCLK, and GND signals to PicoUart before loading; UART TX/RX wiring is separate from SWD.
Like the build wrapper, it defaults to `pico` (RP2040) and `build/firmware-pico` when no board override is supplied.

```sh
tools/firmware/load.sh --board pico
tools/firmware/load.sh --board pico2
```

To provision or recover a clock profile through SWD, use an explicit probe serial:

```sh
tools/firmware/config.py --build-dir build/firmware-pico --probe-serial <probe-serial> read
tools/firmware/config.py --build-dir build/firmware-pico --probe-serial <probe-serial> set-clock 250mhz --unsafe-overclock
tools/firmware/config.py --build-dir build/firmware-pico --probe-serial <probe-serial> erase
```

OpenOCD probe recovery and flash-device compatibility require the physical Debug Probe and target board; automated CI
can validate the command paths but cannot prove USB/SWD recovery behavior. Follow [Releasing](../docs/releasing.md) for
the release HIL and OpenOCD recovery gates.

## Configuration

Shared fixed capacities are defined in [src/config/capacity_config.h](src/config/capacity_config.h). This includes USB
control and CDC endpoint capacities, CDC FIFOs, HID endpoint capacity, and the hardware/PIO UART ring capacities.
TinyUSB-specific mappings remain in [src/config/tusb_config.h](src/config/tusb_config.h).

Do not change a ring capacity without preserving its power-of-two requirement. The PIO RX/TX and hardware UART RX/TX
ring definitions document where that requirement applies.

Detailed firmware behavior lives in:

- [Architecture](../docs/architecture.md) and the [UART subsystem design](../docs/design/uart/README.md)
- [CDC/HID Overview](../docs/design/usb/cdc-hid-overview.md)
- [HID Report Reference](../docs/design/usb/hid-report-reference.md)
- [Control Plane Design](../docs/design/control-plane-design.md)
- [Ring Buffer Design](../docs/design/uart/ring-buffer-design.md)
- [PIO UART Design](../docs/design/uart/pio-uart-design.md)
- Board pin assignments and safe wiring: [Hardware Setup](../docs/hardware-setup.md)
