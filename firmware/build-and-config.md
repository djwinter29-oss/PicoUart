# Firmware Build and Configuration

This folder contains the Pico SDK firmware project `pico_uart` for PicoUart.

Use the root [README](../README.md) as the documentation index. This file keeps
only firmware-local build, load, and configuration notes.

## Build

On Ubuntu/Linux, install CMake, Ninja, an ARM GCC toolchain, and use the
project-local Pico SDK:

```sh
. tools/setup-sdk-env.sh
tools/build.sh --board pico
tools/build.sh --board pico2
```

Optional: stamp a release version into the firmware with `--firmware-version`
(for example `1.2.3` from tag `v1.2.3`). Release version policy is documented
in [Releasing](../docs/releasing.md).

All generated output is stored under the repository-root `build/` directory.
Use a separate build directory per board. The Linux build/load tools accept
`--board` values supported by the installed Pico SDK. They also accept
`--system-clock-khz` to override the system clock for a build. For example:

```sh
tools/build.sh --board pico --system-clock-khz 250000 --unsafe-overclock
tools/build.sh --board pico2 --system-clock-khz 300000 --unsafe-overclock
```

Those examples are intentionally unsafe overrides. Production builds use the
rated 125000 kHz (`pico`) or 150000 kHz (`pico2`) target by default. Pass
`--unsafe-overclock` with an override only for a board-specific, recorded HIL
qualification; CMake otherwise rejects a non-rated clock.

## Load

The Linux load tool programs the ELF remotely through a Raspberry Pi Debug Probe
using CMSIS-DAP OpenOCD. Connect the probe's SWDIO, SWCLK, and GND signals to
PicoUart before loading; UART TX/RX wiring is separate from SWD.

```sh
tools/load.sh --board pico
tools/load.sh --board pico2
```

For an explicitly qualified non-rated clock image, pass the unsafe override to
both the build and load wrappers:

```sh
tools/load.sh --board pico --system-clock-khz 250000 --unsafe-overclock
```

## Configuration

Shared fixed capacities are defined in [src/config/config.h](src/config/config.h).
This includes USB control and CDC endpoint capacities, CDC FIFOs, HID endpoint
capacity, and the hardware/PIO UART ring capacities. TinyUSB-specific mappings
remain in [src/config/tusb_config.h](src/config/tusb_config.h).

Do not change a ring capacity without preserving its power-of-two requirement.
The PIO RX/TX and hardware UART RX/TX ring definitions document where that
requirement applies.

## Notes

- Default board is `pico`.
- Default system-clock targets are 125000 kHz for RP2040 and 150000 kHz for
  RP2350. Higher clock rates are board-specific overrides; validate voltage,
  thermal margin, USB, and UART behavior before using them.
- Startup initializes the selected board's default LED when it defines
  `PICO_DEFAULT_LED_PIN`; the LED starts off.
- The internal ADC temperature sensor is enabled at startup and can be sampled
  through `temperature_read_celsius()`.
- Override the board with `-DPICO_BOARD=<board>` when needed.

Detailed firmware behavior lives in:

- [Architecture](../docs/architecture.md)
- [CDC/HID Overview](../docs/usb/cdc-hid-overview.md)
- [HID Report Reference](../docs/usb/hid-report-reference.md)
- [Control Plane Design](../docs/detail/control-plane-design.md)
- [Ring Buffer Design](../docs/detail/ring-buffer-design.md)
- [PIO UART Design](../docs/detail/pio-uart-design.md)