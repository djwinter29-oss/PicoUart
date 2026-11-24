# Firmware Build and Configuration

This folder holds the Pico SDK firmware project `pico_uart`. This page covers building, loading over SWD, and where the
build-time configuration lives. To flash a released image, see [Releasing](../docs/deployment/releasing.md#flashing-release-artifacts).

## Build

On Ubuntu/Linux (or WSL2), install CMake, Ninja, and an ARM GCC toolchain, then:

```sh
. tools/firmware/setup-sdk-env.sh
tools/firmware/build.sh                 # Pico (RP2040)  -> build/firmware-pico
tools/firmware/build.sh --board pico2   # Pico 2 (RP2350) -> build/firmware-pico2
```

- `--board` overrides `PICO_BOARD`, and the default is `pico`. Each board builds into its own directory under `build/`.
- The scripts use an exported `PICO_SDK_PATH` if set. Otherwise they use the pinned SDK checkout in `.pico-sdk/`, which
  `setup-sdk-env.sh` clones on first use.
- `--firmware-version 1.2.3` stamps a release version, which is reported over HID.
- `--disable-hid-reset` removes the standalone HID reboot action. Save-and-reboot is not affected.
- `pico_w` and `pico2_w` build USB/UART-only images, with Wi-Fi and Bluetooth disabled; see the
  [wireless board notes](../docs/hardware-setup.md#wireless-boards).
- If the board, SDK, generator, version, or HID-reset option changes, the wrapper clears stale CMake state
  automatically.

### Build Checks

CI runs both checks below on every firmware build, and you can run them locally too:

- [`check-firmware-budget.py`](../tools/release/check-firmware-budget.py) caps static SRAM allocation at 192 of 264 KiB
  (RP2040) and 384 of 520 KiB (RP2350). It also checks that the image fits before the reserved configuration sector.
  These are allocation limits, not stack or latency measurements.
- The voltage-policy test checks that the firmware never writes core-voltage registers, for any clock profile:

  ```sh
  PICO_UART_VOLTAGE_BUILD_DIRS="$PWD/build/firmware-pico:$PWD/build/firmware-pico2" \
    python3 -m pytest -c pyproject.toml \
    tests/firmware/test_system_clock_voltage_policy.py -k real_sdk -o addopts='' -q
  ```

  It inspects the real SDK startup object and the linked ELF. It does not measure voltage, temperature, or stability.

## Load Over SWD

`load.sh` programs the ELF through a Raspberry Pi Debug Probe (CMSIS-DAP OpenOCD), rebuilding first if needed. Connect
SWDIO, SWCLK, and GND; SWD wiring is separate from the UART wiring.

```sh
tools/firmware/load.sh --board pico
tools/firmware/load.sh --board pico2
```

Probe recovery and QSPI flash details are in [Releasing](../docs/deployment/releasing.md#debug-probe-elf).

## Clock Profile

The firmware image has no fixed clock; build-time clock overrides are rejected. At boot it reads a stored clock
profile from the last flash sector. If the record is missing or invalid, it uses SDK defaults. The profile is set from
the dashboards (HID save-and-reboot) or, when USB is unavailable, over SWD:

```sh
tools/firmware/config.py --build-dir build/firmware-pico --probe-serial <probe-serial> read
tools/firmware/config.py --build-dir build/firmware-pico --probe-serial <probe-serial> set-clock 250mhz --unsafe-overclock
tools/firmware/config.py --build-dir build/firmware-pico --probe-serial <probe-serial> erase
```

Profile meanings and the save rules are in the [HID reference](../docs/design/usb/hid-report-reference.md#boot-configuration).
Non-default profiles are overclocks. Qualify them on the exact board before relying on them.

## Configuration

| What | Where |
| --- | --- |
| Port-to-backend mapping, GPIOs, and startup line coding | [src/config/uart_config.c](src/config/uart_config.c) |
| Hardware UART flow control (per port, off by default) | [src/uart/hw/hw_uart_board_config.c](src/uart/hw/hw_uart_board_config.c) |
| PIO pin flags: RX pull-up, idle-high check, RTS/CTS | [src/uart/pio/pio_uart_board_config.c](src/uart/pio/pio_uart_board_config.c) |
| Ring, CDC FIFO, and endpoint capacities | [src/config/capacity_config.h](src/config/capacity_config.h) |
| TinyUSB mapping | [src/config/tusb_config.h](src/config/tusb_config.h) |

Ring capacities must stay powers of two. Changing a pin table changes the
[wiring contract](../docs/hardware-setup.md#port-map), so update that page in the same change.

The design behind all of this starts at the [architecture overview](../docs/design/architecture.md). Native tests are
in [tests/](tests/README.md).
