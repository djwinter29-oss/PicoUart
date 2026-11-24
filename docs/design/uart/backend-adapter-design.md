# Backend Operations Contract

PicoUart has two UART implementations: the hardware UART (PL011 with DMA) and the PIO UART (state machines with DMA).
Both implement one operations contract. The UART runtime, the logical-port API, and the control plane call only that
contract, with no backend-specific branches. USB and board code never call it directly.

## What the Contract Covers

- **Lifecycle:** start, stop, restart, and readiness.
- **Worker I/O:** one service pass that moves data between the rings and the hardware.
- **Rings:** access to the port's RX and TX rings ([ring design](ring-buffer-design.md)).
- **Line coding:** checking whether a format is supported, and applying it once safe
  ([control plane](../control-plane-design.md)).
- **Telemetry:** traffic and error counters, and coherent RX progress snapshots.

Each backend provides a constant operations table. An incomplete table or an unknown backend fails safely at startup.
Queries on an inactive backend return neutral values, so callers check readiness before treating results as live.

## Ownership

- The board table picks the backend and pins for each port. Backends read pins from that constant table rather than
  copying them.
- Each backend owns its registers, its DMA channels or state machines, and its rings. Nothing outside the backend
  touches that state.
- Applied baud rate and framing live in the backend's mutable state, separate from the board defaults, because the host
  can change them.
- RX ring storage is supplied by the port owner and lives as long as the backend does.

## Lifecycle Rules

- **Startup is all-or-nothing.** If any backend fails, the ones already started are rolled back and the worker does not
  launch.
- **Starting an active backend is refused**, which leaves its DMA, rings, and settings untouched.
- **Each start seeds a complete format.** That is either the queued request or the last applied settings. A failed start
  leaves the applied settings unchanged, so the reported format always matches the hardware when the port is `READY`.
- **Restart is a real stop and start.** It discards queued bytes; it is not just a buffer clear.
- **Counters outlive restarts.** Traffic and error counters, ring peaks, and overflow counts last from boot. Restarting
  a port does not reset them.
- **DMA channels are held while active.** Each running port keeps one RX and one TX channel. Six ports use all 12 RP2040
  channels, so any future DMA feature has to find another way.

## Line Coding

Each backend answers two separate questions: whether it supports a format at all, and whether it is safe to apply the
change right now. Both backends receive the same backend-neutral request.

- The **hardware UART** supports the standard data-bit, parity, and stop-bit options. It checks the baud rate against
  the PL011 divider and an error tolerance.
- The **PIO UART** is 8N1 only. It accepts a baud rate only if both its TX and RX clock dividers can represent it.

Requests that are malformed or cannot be met by the clock are rejected before any hardware is touched.
