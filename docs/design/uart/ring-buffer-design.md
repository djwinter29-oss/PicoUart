# Ring Buffer Design

## Purpose and Scope

Each logical UART port uses a TX ring for USB-to-UART data and an RX ring for UART-to-USB data. This document covers
their ownership, synchronization, overflow/backpressure policy, and relationship to DMA. The overall bridge is described
in [UART Subsystem](README.md); cross-core synchronization is detailed in
[Multicore Ownership Design](../multicore-ownership-design.md).

## Current Design: Split RX And TX Rings Per Port

Each logical port owns two separate ring buffers:

- one RX ring for UART to USB traffic
- one TX ring for USB to UART traffic

### Ownership and Synchronization

| Ring | Producer | Consumer |
| --- | --- | --- |
| TX | USB bridge | UART backend |
| RX | UART backend/DMA | USB bridge |

Each ring is single-producer/single-consumer. Core 0 owns USB and bridge-side operations; core 1 owns UART service. The
core ownership and cursor memory-ordering guarantees are detailed in
[Multicore Ownership Design](../multicore-ownership-design.md).

## Capacity

Ring capacities are configured in [capacity_config.h](../../../firmware/src/config/capacity_config.h). Sizes must remain
powers of two; compile-time checks keep DMA ring selectors consistent. Capacity changes affect RAM use and the rate at
which overflow or backpressure occurs.

## Overflow and Backpressure Policy

RX is a live stream, not an archival logger. On overrun, the consumer advances past overwritten bytes, preserves newer
data, and records the loss. The producer never changes the consumer cursor. HID exposes overrun health and cumulative
dropped-byte counts; see the [HID Report Reference](../usb/hid-report-reference.md).

When the TX ring is full, the bridge leaves unread CDC OUT data queued and stops reading that port until space is
available. This preserves host-byte ordering and lets USB apply backpressure instead of silently dropping queued data.

## Ring API and DMA

The backend-agnostic API is declared in
[ring_buffer.h](../../../firmware/src/uart/ring_buffer/ring_buffer.h). It exposes contiguous spans with explicit commit
operations; commits must stay within the most recently reserved span. A copied RX
snapshot must be validated before the consumer cursor advances because live DMA can overwrite the source span.

RX DMA publishes progress into the RX ring without an intermediate queue. TX DMA consumes a reserved contiguous span
and commits it only after transfer completion. Backend-specific DMA behavior belongs in the
[Hardware UART](hardware-uart-design.md) and [PIO UART](pio-uart-design.md) designs.

## Observability

The ring tracks high-water occupancy and RX loss. HID exposes a compact high-water mark and sticky overrun health;
cumulative dropped-byte counts are defined in the [HID Report Reference](../usb/hid-report-reference.md).

## Validation

Host tests cover ring and bridge invariants, but do not reproduce live DMA or multicore timing. See the
[firmware testing guide](../../development/firmware-testing.md) for build and hardware validation boundaries.

## Alternative Considered: Shared Memory Pool

A shared memory pool with descriptors could reduce reserved RAM for idle ports and preserve packet boundaries
explicitly. It was rejected for the current UART bridge because it adds allocator, descriptor, DMA restart, and
overflow-debugging complexity without solving a measured problem. Revisit it only if fixed per-port rings become a
real RAM-pressure issue or the firmware gains packet-oriented processing.
