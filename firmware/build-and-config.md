# Firmware Build and Configuration

This folder contains the Pico SDK firmware project `pico_uart` for PicoUart.

Use the root [README](../README.md) as the documentation index. This file keeps only firmware-local build, load, and
configuration notes.

## Build

On Ubuntu/Linux, install CMake, Ninja, an ARM GCC toolchain, and use the project-local Pico SDK:

```sh
. tools/firmware/setup-sdk-env.sh
tools/firmware/build.sh --board pico
tools/firmware/build.sh --board pico2
```

Optional: stamp a release version into the firmware with `--firmware-version` (for example `1.2.3` from tag `v1.2.3`).
Release version policy is documented in [Releasing](../docs/releasing.md).

All generated output is stored under the repository-root `build/` directory. Use a separate build directory per board.
The Linux build/load tools accept `--board` values supported by the installed Pico SDK. They also accept
`--system-clock-khz` to override the system clock for a build. For example:

The scripts honor an exported `PICO_SDK_PATH`; otherwise they use the pinned repository-local `.pico-sdk` checkout.
`load.sh` uses the same SDK selection as `build.sh` when it rebuilds an image.

```sh
tools/firmware/build.sh --board pico --system-clock-khz 250000 --unsafe-overclock
tools/firmware/build.sh --board pico2 --system-clock-khz 280000 --unsafe-overclock
```

Those examples are intentionally unsafe overrides. Production builds use the rated 125000 kHz (`pico`) or 150000 kHz
(`pico2`) target by default. Pass `--unsafe-overclock` with an override only for a board-specific, recorded HIL
qualification; CMake otherwise rejects a non-rated clock.

PR and release workflows build overrides in addition to the rated defaults: `pico` at 250 MHz (`pico-250mhz`) and
`pico2` at 280 MHz (`pico2-280mhz`) in both workflows. Neither overclock target writes the core voltage; both boards
retain the regulator setting present on entry. The build ceiling is 400000 kHz for RP2040 and 500000 kHz for RP2350, not
a stability guarantee. The promote HIL gate covers the rated images. See [Releasing](../docs/releasing.md).

Some Pico 2 boards may not support 300 MHz reliably: one tested board failed USB bring-up with the v0.5.0
300 MHz artifact, even after a power cycle, while its standard 150 MHz image passed the fixture tests.
The development target is now 280 MHz; this reduction is not a stability guarantee and still needs exact-board HIL.
Existing v0.5.0 release files are unchanged. See [Releasing](../docs/releasing.md) for the observed failure and limits.

The firmware target explicitly sets `SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST=0` for both application and SDK startup sources,
including the SDK's own pre-`main()` clock init (`runtime_init_clocks.c`, board default 125/150 MHz). The SDK
(`hardware/clocks.h`) only ever defaults this to `1` when compiled with `PICO_RP2040 && SYS_CLK_HZ==200MHz`; this
project never sets `SYS_CLK_HZ`, so that default is already `0` for every board/clock built here, and this define is a
defensive pin rather than a behavior change. `system_init_clock()` then requests the application target via
`set_sys_clock_khz`, which does not touch the regulator either way. Neither path writes or restores a specific voltage;
whatever voltage was present on entry is left alone.

Each firmware matrix leg in PR check CI runs this check automatically, right after `verify-build.py`, against its own
build directory. After building locally, run the same check yourself to inspect the real SDK startup object, resolved
preprocessor policy, and linked ELF (not just the mock application test):

```sh
PICO_UART_VOLTAGE_BUILD_DIRS="$PWD/build/pico:$PWD/build/pico2" \
  python3 -m pytest -c pyproject.toml \
  tests/firmware/test_system_clock_voltage_policy.py -k real_sdk -o addopts='' -q
```

Use your actual completed build directories. This check fails if SDK automatic voltage adjustment is enabled or the
startup object/ELF contains a regulator write or voltage-limit-bypass symbol. It does not execute Boot ROM or validate
physical voltage, thermal margins, or hardware stability.

Changing board, SDK path, generator, firmware version, system clock, HID-reset option, or unsafe-overclock option causes
the build wrapper to reset stale CMake cache state before reconfiguring.

## Load

The Linux load tool programs the ELF remotely through a Raspberry Pi Debug Probe using CMSIS-DAP OpenOCD. Connect the
probe's SWDIO, SWCLK, and GND signals to PicoUart before loading; UART TX/RX wiring is separate from SWD.

```sh
tools/firmware/load.sh --board pico
tools/firmware/load.sh --board pico2
```

For an explicitly qualified non-rated clock image, pass the unsafe override to both the build and load wrappers:

```sh
tools/firmware/load.sh --board pico --system-clock-khz 250000 --unsafe-overclock
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

## Notes

- Default board is `pico`.
- Default system-clock targets are 125000 kHz for RP2040 and 150000 kHz for RP2350. Higher clock rates are
  board-specific overrides. Startup never writes the core voltage; it preserves the regulator setting on entry rather
  than measuring or restoring a specific voltage. Neither development overclock image (RP2040 250 MHz, RP2350 280 MHz)
  is qualified for stability, thermal margin, or lifetime without exact-board HIL over the intended workload and
  temperature range.
- Startup initializes the selected board's default LED when it defines `PICO_DEFAULT_LED_PIN`; the LED starts off.
- The internal ADC temperature sensor is enabled at startup and can be sampled through `temperature_read_celsius()`.
- Temperature uses the RP2 nominal sensor formula on both supported MCU families; validate its board-level accuracy
  during HIL testing before using it as a calibrated measurement.
- Override the board with `-DPICO_BOARD=<board>` when needed.

Detailed firmware behavior lives in:

- [Architecture](../docs/architecture.md)
- [CDC/HID Overview](../docs/design/usb/cdc-hid-overview.md)
- [HID Report Reference](../docs/design/usb/hid-report-reference.md)
- [Control Plane Design](../docs/design/control-plane-design.md)
- [Ring Buffer Design](../docs/design/ring-buffer-design.md)
- [PIO UART Design](../docs/design/pio-uart-design.md)
