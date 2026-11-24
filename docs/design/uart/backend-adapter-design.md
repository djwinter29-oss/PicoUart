# Backend Operations Contract

PicoUart has two UART implementations: the hardware UART (PL011 with DMA) and the PIO UART (state machines with DMA).
Both implement one operations contract. The UART runtime, the logical-port API, and the control plane call only that
contract, with no backend-specific branches. USB and board code never call it directly.

## What the Contract Covers

The contract covers lifecycle (start, stop, restart, readiness), a single worker pass that moves data between the
rings and the hardware, access to the port's RX and TX [rings](ring-buffer-design.md), line coding (whether a format is
supported, and applying it once it is safe; see the [control plane](../control-plane-design.md)), and telemetry: traffic
and error counters plus coherent RX progress snapshots.

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

Startup is all-or-nothing: if any backend fails, the ones already started are rolled back and the worker does not
launch. Starting a backend that is already active is refused, so its DMA, rings, and settings stay untouched.

Every start is seeded with a complete format, either the queued request or the last applied settings. A failed start
leaves the applied settings alone, so the reported format always matches the hardware while the backend is running.

Restart is a real stop and start and discards queued bytes. Counters survive it: traffic and error counts, ring peaks,
and overflow counts run from boot.

## Line Coding

Each backend answers two separate questions: whether it supports a format at all, and whether it is safe to apply the
change right now. Both backends receive the same backend-neutral request.

- The **hardware UART** supports the standard data-bit, parity, and stop-bit options. It checks the baud rate against
  the PL011 divider and an error tolerance.
- The **PIO UART** is 8N1 only. It accepts a baud rate only if both its TX and RX clock dividers can represent it.

Requests that are malformed or cannot be met by the clock are rejected before any hardware is touched.
