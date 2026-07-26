# Firmware

This folder contains the Pico SDK firmware project `pico_uart` for PicoUart.

The current USB implementation enumerates 6 CDC ports and 1 HID status-monitor interface.
Each CDC port is bridged to one logical UART backend.

## Build

1. Install CMake, Ninja, an ARM GCC toolchain, and the Pico SDK.
2. Set `PICO_SDK_PATH` to your `pico-sdk` checkout.
3. Configure and build:

```powershell
cmake -S firmware -B build/firmware -G Ninja
cmake --build build/firmware
```

For the project-local Pico SDK and board-specific builds on Linux:

```sh
. tools/linux/setup-sdk-env.sh
tools/linux/build.sh --board pico
tools/linux/build.sh --board pico2
```

Optional: stamp a release version into the firmware with `--firmware-version`
(for example `1.2.3` from tag `v1.2.3`). That value is published through HID
as `MAJOR.MINOR.PATCH`, while USB `bcdDevice` receives major.minor BCD only
(`1.2.3` → `0x0102`). Untagged local builds default to `0.0.0-dev`.

On Windows PowerShell, download the same project-local SDK and build with:

```powershell
. .\tools\windows\setup-sdk-env.ps1
.\tools\windows\build.ps1 -Board pico
.\tools\windows\build.ps1 -Board pico2
```

All generated output is stored under the repository-root `build/` directory.
Use a separate build directory per board. The Linux and Windows build/load tools
accept `--board` or `-Board` values supported by the installed Pico SDK.
They also accept `--system-clock-khz` or `-SystemClockKhz` to override the
system clock for a build. For example:

```sh
tools/linux/build.sh --board pico --system-clock-khz 250000
tools/linux/build.sh --board pico2 --system-clock-khz 300000
```

## Load

The Linux and Windows load tools program the ELF remotely through a Raspberry
Pi Debug Probe using CMSIS-DAP OpenOCD. Connect the probe's SWDIO, SWCLK, and
GND signals to PicoUart before loading; UART TX/RX wiring is separate from SWD.

```sh
tools/linux/load.sh --board pico
tools/linux/load.sh --board pico2
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


- Default system-clock targets are 250000 kHz for RP2040 and 300000 kHz for
	RP2350. Override them only after validating the board and attached hardware
	at the selected frequency.
- Startup initializes the selected board's default LED when it defines
  `PICO_DEFAULT_LED_PIN`; the LED starts off.
- The internal ADC temperature sensor is enabled at startup and can be sampled
  through `temperature_read_celsius()`.
- HID report ID `3` exposes temperature and firmware version; report ID `4`
	accepts board-scoped LED-toggle and watchdog-reset commands. HID does not
	own UART configuration. See [the HID protocol](../docs/hid-monitor.md) and
	[the Python host utility](../host/python/README.md).
- Override the board with `-DPICO_BOARD=<board>` when needed.
- Firmware initializes all 6 UART backends on core 1 before starting TinyUSB on core 0.
- A dedicated second core owns UART backend init, polling, and control operations.
- Core 0 talks to the UART core through a small single-slot mailbox for cross-core control.
- HID status reports expose per-port health (including sticky RX overrun), ring high-water
  marks, temperature, and firmware `MAJOR.MINOR.PATCH`.
- Deferred control requests first report as queued, then transition to success after the
  worker applies them, or to `CONTROL_ERROR` if apply cannot complete within 1 second.
- Core 1 initializes UART backends, polls hardware UART DMA and PIO UART RX/TX, and
  applies deferred control requests; core 0 services TinyUSB and bridges CDC rings.
- The current firmware maps CDC0-CDC5 to 2 hardware UART backends and 4 PIO UART backends.
- CDC line coding is parsed on core 0, stored as pending per-port configuration, and applied on core 1 by the UART worker.
- PIO UART RX uses persistent DMA into the per-port RX ring (DMA IRQ1 re-arm).
  PIO TX fills the joined FIFO for short queues and lazily claims DMA only when
  deeper backlog makes it worthwhile.
- Hardware UART ports accept supported baud/data/parity/stop updates and enable RTS/CTS;
  PIO UART ports remain 8N1-only with reserved RTS/CTS pins.
- PIO UART line-coding changes are deferred on the worker core until the port reaches a safe idle point, to avoid discarding queued traffic.
- PIO UART RX validates stop bits and counts framing errors (see `docs/detail/pio-uart-design.md`).
- CDC line-coding rejects are visible through HID `CONTROL_ERROR` because TinyUSB accepts `SET_LINE_CODING` before firmware validation (`docs/hid-monitor.md`).
- PIO baud/format rejects fail fast on core 0 (no deferred 1 s pause) when the divider is out of range or the request is not 8N1.
- Cross-core mailbox: core 0 waits up to 20 ms for the slot to be idle and 100 ms for a response after posting. A response timeout leaves the request in flight (orphan); the next call waits for idle rather than posting over it.
- HID reset is a two-step arm/reset sequence; disable with `-DPICO_UART_ALLOW_HID_RESET=0`.
- CDC DTR updates HID `opened` only and does not gate bridging; host CDC RTS is ignored.
- HID does not yet report full ring occupancy/overflow **counts** (only high-water blocks and a sticky overrun bit).
