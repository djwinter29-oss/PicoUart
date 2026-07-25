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
tools/linux/build.sh --board pico --system-clock-khz 125000
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

## Notes

- Default board is `pico`.

- Default system-clock targets are 125000 kHz for RP2040 and 300000 kHz for
	RP2350. Override them only after validating the board and attached hardware
	at the selected frequency.
- Override the board with `-DPICO_BOARD=<board>` when needed.
- Firmware startup asserts that all 6 UART backends initialize before USB enumeration begins.
- A dedicated second core owns UART backend init, polling, and control operations.
- Core 0 talks to the UART core through a small single-slot mailbox for cross-core control.
- HID status reports expose the worker-core state, the last mailbox command result, and per-port init/control-error flags.
- Deferred control requests first report as queued, then transition to success only after the worker actually applies them.
- Core 1 initializes UART backends and applies deferred control requests; core 0 services hardware UART DMA and PIO UART RX/TX state while bridging TinyUSB.
- The current firmware maps CDC0-CDC5 to 2 hardware UART backends and 4 PIO UART backends.
- CDC line coding is parsed on core 0, stored as pending per-port configuration, and applied on core 1 by the UART worker.
- PIO UART RX and TX are polled on core 0. TX fills the joined FIFO for short queues and lazily claims DMA only when deeper backlog makes it worthwhile.
- Hardware UART ports accept supported baud/data/parity/stop updates; PIO UART ports remain 8N1-only.
- PIO UART line-coding changes are deferred on the worker core until the port reaches a safe idle point, to avoid discarding queued traffic.
- PIO UART RX drops frames with an invalid stop bit instead of forwarding corrupted bytes.
- CDC line-state changes such as DTR and RTS are currently ignored.
- The HID monitor currently reports topology metadata, not live error or overflow counters.
