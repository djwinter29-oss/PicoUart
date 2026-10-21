# Ring Buffer Design

## Purpose and Scope

Each logical UART port uses a TX ring for USB-to-UART data and an RX ring for UART-to-USB data. This document covers
their ownership, synchronization, overflow/backpressure policy, and relationship to DMA. The overall bridge is described
in [UART Subsystem](README.md); cross-core synchronization is detailed in
[Architecture: Inter-Core Coordination](../architecture.md#inter-core-coordination).

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
[Architecture: Ring Ownership](../architecture.md#ring-ownership).

## Capacity

Each port has fixed RX and TX capacities. Sizes must remain powers of two, and DMA-backed storage must meet the
backend's alignment requirements. Capacity changes affect reserved RAM and the rate at which overflow or backpressure
occurs; they do not change the ownership or loss policy.

## Overflow and Backpressure Policy

RX is a live stream, not an archival logger. On overrun, the consumer advances past overwritten bytes, preserves newer
data, and records the loss. The producer never changes the consumer cursor. HID exposes overrun health and cumulative
dropped-byte counts; see the [HID Report Reference](../usb/hid-report-reference.md).

When the TX ring is full, the bridge leaves unread CDC OUT data queued and stops reading that port until space is
available. This preserves host-byte ordering and lets USB apply backpressure instead of silently dropping queued data.

## Ring API and DMA

The ring contract exposes contiguous spans with explicit commit operations. Each side has at most one current span
reservation; requesting another replaces that side's previous reservation. A commit must stay within the current
reservation and satisfy its cursor-validity checks.

RX DMA writes ring storage directly, and the backend publishes observed progress into the producer cursor. Before
forwarding a copied RX span to USB, the bridge validates the reservation against live DMA progress and a coherent
backend snapshot. It retires only bytes accepted by the USB writer; overwritten data is handled by the loss policy.

TX DMA holds a reserved contiguous span until DMA completion, then commits consumption. That frees ring storage but
does not establish wire-level TX idle; the peripheral can still be shifting bytes. Backend-specific DMA behavior belongs
in the [Hardware UART](hardware-uart-design.md) and [PIO UART](pio-uart-design.md) designs.

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
