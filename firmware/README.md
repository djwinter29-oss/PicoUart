# Firmware

This folder contains the Pico SDK firmware project `pico_uart` for PicoUart.

The current USB implementation enumerates 6 CDC ports and 1 HID status-monitor interface.
Each CDC port is bridged to one logical UART backend.

## Build

1. Install CMake, Ninja, an ARM GCC toolchain, and the Pico SDK.
2. Set `PICO_SDK_PATH` to your `pico-sdk` checkout.
3. Configure and build:

```powershell
cmake -S firmware -B firmware/build -G Ninja
cmake --build firmware/build
```

## Notes

- Default board is `pico`.
- Override the board with `-DPICO_BOARD=<board>` when needed.
- Firmware startup asserts that all 6 UART backends initialize before USB enumeration begins.
- A dedicated second core owns UART backend init, polling, and control operations.
- Core 0 talks to the UART core through a small single-slot mailbox for cross-core control.
- HID status reports expose the worker-core state, the last mailbox command result, and per-port init/control-error flags.
- Deferred control requests first report as queued, then transition to success only after the worker actually applies them.
- Core 1 is the only core that touches UART, DMA, PIO, and backend helper state; core 0 only moves data through shared RX/TX rings for TinyUSB.
- The current firmware maps CDC0-CDC5 to 2 hardware UART backends and 4 PIO UART backends.
- CDC line coding is parsed on core 0, stored as pending per-port configuration, and applied on core 1 by the UART worker.
- PIO UART is intentionally asymmetric: RX is IRQ-driven on core 1, while TX uses a hybrid core-1 service path that fills the joined TX FIFO for short queues and lazily claims DMA only when deeper backlog makes it worthwhile.
- Hardware UART ports accept supported baud/data/parity/stop updates; PIO UART ports remain 8N1-only.
- PIO UART line-coding changes are deferred on the worker core until the port reaches a safe idle point, to avoid discarding queued traffic.
- PIO UART RX drops frames with an invalid stop bit instead of forwarding corrupted bytes.
- CDC line-state changes such as DTR and RTS are currently ignored.
- The HID monitor currently reports topology metadata, not live error or overflow counters.
