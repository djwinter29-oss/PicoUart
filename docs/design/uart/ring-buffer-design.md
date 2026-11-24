# Ring Buffer Design

Each UART port has two fixed ring buffers between the USB side (core 0) and the UART side (core 1):

| Ring | Direction | Producer | Consumer |
| --- | --- | --- | --- |
| TX | USB to UART | USB bridge (core 0) | UART backend (core 1) |
| RX | UART to USB | UART backend / RX DMA (core 1) | USB bridge (core 0) |

Each ring has exactly one producer and one consumer, and needs no lock; see
[ring ownership](../architecture.md#ring-ownership).

## Capacity

Ring sizes are fixed at build time and must be powers of two. RX storage is also aligned to its size so RX DMA can
wrap around it in hardware. That alignment applies only to the RX storage, not to the surrounding port structures.
Capacity changes how much RAM is reserved and how soon overflow or backpressure starts. It does not change the
ownership or loss policy.

The buffer sizes are not the whole RAM cost. Measure the linked map before shrinking them; the procedure is in the
[firmware guide](../../development/firmware-testing.md#buffer-capacity-measurement).

## Overflow and Backpressure

The two directions handle a full ring differently, on purpose:

- **TX full:** the bridge stops reading that CDC port and leaves the data in USB. The host is pushed back and nothing is
  lost or reordered.
- **RX full:** RX is a live stream, not a logger. The UART cannot pause, so the oldest unread bytes are overwritten and
  newer data is kept. The consumer skips past the overwritten bytes and counts them. HID reports this as an overrun
  health bit and a cumulative dropped-byte count.

A hardware UART FIFO overrun that happens before data reaches the ring is counted differently. The PL011 only latches a
sticky flag, so it shows up as an `rx_error_count` event, not a dropped-byte count.

## DMA Interaction

The ring API hands out contiguous spans that the caller fills or drains and then commits. Each side has at most one
outstanding span.

- **RX DMA** writes straight into the ring storage, and the backend publishes its progress as the producer cursor.
  Before sending a span to USB, the bridge checks it against live DMA progress, so data overwritten mid-copy is counted
  as lost rather than forwarded.
- **TX DMA** holds its span until the transfer completes, then commits it. Completion frees ring space, but it does not
  mean the bytes have left the TX pin.

## Observability

Each ring tracks its high-water mark and RX loss. HID exposes the peak occupancy, an overrun health bit, and cumulative
dropped bytes.

## Alternative Considered: Shared Memory Pool

A shared pool with descriptors would save RAM on idle ports and keep packet boundaries. It was rejected because it adds
allocator, descriptor, and DMA-restart complexity without solving a measured problem. Revisit it if fixed rings become
a real RAM constraint, or if the firmware ever needs packet-oriented processing.
