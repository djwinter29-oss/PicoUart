# PIO UART Design

## Scope

This document describes the current PIO UART backend used for logical UART ports 2-5.
It focuses on the worker-core ownership model, the RX polling path, and the hybrid TX policy
that combines FIFO polling with DMA.

## Ownership Model

- core 0 owns TinyUSB and copies bytes into or out of shared per-port rings
- core 1 owns PIO state machines, DMA channels, and backend control changes
- RX and TX rings remain the only data-plane contract between the USB side and the PIO backend

This keeps TinyUSB isolated from hardware service details and preserves a single producer and
single consumer per ring direction.

## RX Path

The RX path is polling-driven on core 1. Each worker sweep drains every configured
PIO RX FIFO into its matching RX ring.

Current RX behavior:

- when the RX ring is full, the backend preserves newest valid bytes by advancing the consumer
  and incrementing the overflow counter

## TX Path

The TX path is hybrid and always owned by core 1.
It uses one per-port decision per worker poll:

1. if a TX DMA transfer is active, poll for completion and commit that span when done
2. if no transfer is active and TX backlog is above the DMA threshold, launch a bounded DMA transfer
3. otherwise, drain bytes directly into the joined TX FIFO from the poll loop

This keeps the data path straightforward while still avoiding DMA setup overhead on short bursts.

### Poll Mode

When the shared TX ring holds only a short queue, core 1 pushes bytes directly into the joined PIO TX FIFO
from the poll loop.
This keeps small bursts cheap and avoids paying DMA setup cost for a handful of bytes.
The TX poll path runs alongside the next worker-sweep RX FIFO drain.

### DMA Preferred Mode

When TX backlog crosses the configured DMA threshold, core 1 tries to claim a
DMA channel and launches from `ring_buffer_read_span()` into the matching PIO
TX FIFO. If no channel is available, the FIFO poll path continues to drain the
queue.
Each launch is explicitly bounded to a fixed maximum chunk size so one service pass cannot monopolize the
worker for an unbounded contiguous ring span.

### DMA Active Mode

While a TX DMA transfer is in flight, the transfer owns exactly `tx_dma_bytes_in_flight` bytes from the
TX ring.
When the transfer completes, the backend commits those bytes, updates the
TX-through-DMA counter, and releases the DMA channel.

## Hysteresis

The current implementation uses one per-port DMA threshold:

- DMA start threshold: 64 bytes by default

and one fixed bound for per-launch DMA work:

- DMA max transfer size: 256 bytes by default

This keeps the control logic simple and bounds each DMA submission.

## Why Hybrid TX

The joined PIO TX FIFO is still small compared with sustained USB-originated bursts across multiple ports.
Pure polling becomes more expensive as multiple UART lanes build backlog.
Pure DMA for every burst would also be wasteful because setup cost dominates tiny writes.
The hybrid policy keeps the simple path for small transfers and uses DMA only when there is enough
queued work to amortize that setup cost.

## Counters And Observability

The backend maintains counters for bytes sent through the poll path and DMA
path. The compact HID input report exposes the aggregate controller TX and RX
byte deltas; it does not distinguish PIO poll and DMA TX traffic.

## Control Operations

Line-coding changes are now owned by the worker core and applied only after the port has quiesced.
For the current PIO backend that still means 8N1-only framing, but the worker no longer needs the host
request path itself to busy-wait for that idle window.
Before reconfiguring a PIO UART backend, core 1:

- pauses new USB-to-UART writes for that port through the shared control-pending state
- drains any pending RX bytes into the RX ring
- harvests TX DMA completion if one just finished
- retries on later worker sweeps while TX DMA is still active, TX ring data remains, the TX FIFO is not empty,
  or the RX FIFO is not empty

The RX line level is not part of the mandatory gate. Requiring the RX pin to read high before reconfiguring
can stall a deferred baud change forever when the pin floats or is externally held low, so that stricter
check is opt-in per port through `PIO_UART_DRIVER_PIN_FLAG_REQUIRE_RX_IDLE_HIGH` and is only appropriate for
boards that can guarantee an idle-high RX line.

This keeps the reconfiguration path conservative, avoids silently discarding queued traffic, and keeps the
worker loop responsive while the port drains toward a safe reconfiguration point.

## Current Limits

- 8N1 only
- no parity handling
- no RTS/CTS runtime behavior (pins reserved; hardware UART0/UART1 own RTS/CTS)
- no framing or stop-bit validation
- TX DMA thresholds are configurable per port but still use static defaults rather than adaptive tuning
- TX fairness across the 4 PIO ports is improved by worker-loop round-robin polling, but still lacks an explicit scheduler
- per-launch DMA size is a fixed bound today, not adaptive to live peer pressure

## Follow-Up Options

1. Tune DMA threshold and max DMA launch size from measured worker-core load and end-to-end latency.
2. Upgrade the PIO RX program if line-noise tolerance becomes a priority.
3. Add an explicit worker-side TX scheduler if multiple PIO ports sustain high TX pressure at the same time.
