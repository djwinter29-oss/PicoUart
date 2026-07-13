# Ring Buffer Design

## Goal

PicoUart needs an efficient data path between:

- TinyUSB CDC on the USB side
- DMA-backed hardware UART and PIO UART on the target side

Each logical UART port needs buffering in both directions:

- USB to UART transmit path
- UART to USB receive path

The design should minimize CPU work, reduce unnecessary memory copies, and remain simple enough to debug on RP2040 and RP2350.

## End-To-End Scope

This document defines the full end-to-end buffering path from:

- host USB CDC OUT traffic to UART TX pins
- UART RX pins to host USB CDC IN traffic
- HID status reporting for buffer health and flow state

The design includes:

- the generic ring-buffer helper
- the per-port bridge state
- TinyUSB ownership rules
- UART DMA ownership rules
- polling and completion flow
- buffer overflow policy
- status visibility through HID

## Current Implementation Status

Implemented now:

- generic fixed-size ring-buffer helper under `firmware/src/ring_buffer`
- hardware UART backend integration for both RX and TX ring usage
- RX DMA producer publishing into the RX ring
- TX DMA draining contiguous spans from the TX ring
- PIO UART backend integration with the ring helper and hybrid TX drain policy
- USB CDC bridge layer using the TX and RX rings end to end
- multicore split where core 0 owns TinyUSB and shared rings while core 1 owns UART hardware service

Not implemented yet:

- HID reporting of ring occupancy, overflow, and watermark counters
- flow-control state in the bridge layer
- line-coding updates beyond baud rate
- parity handling and advanced framing features

## Requirements

- 6 logical UART ports
- 2 hardware UART backends
- 4 PIO UART backends
- TinyUSB owned by one execution context
- DMA used for UART RX and UART TX
- Clear ownership of producers and consumers
- Predictable backpressure behavior
- One execution context that owns all TinyUSB API calls
- A design that can scale from 1 port to 6 ports without changing the buffer model
- HID-visible counters for overflow and watermarks

## Design A: Split RX And TX Rings Per Port

Each logical port owns two separate ring buffers:

- one RX ring for UART to USB traffic
- one TX ring for USB to UART traffic

### Data Flow

For USB to UART:

1. TinyUSB receives CDC OUT bytes.
2. The firmware copies those bytes into the port TX ring.
3. UART TX DMA drains the TX ring one contiguous span at a time.

For UART to USB:

1. UART RX DMA fills the port RX ring.
2. The firmware checks the DMA write position.
3. TinyUSB sends contiguous RX ring data to the matching CDC IN endpoint.

### Ownership Model

- TX ring producer: USB side
- TX ring consumer: UART backend
- RX ring producer: UART backend
- RX ring consumer: USB side

With the current multicore split:

- core 0 owns TinyUSB and the ring-side copy operations
- core 1 owns UART, DMA, PIO, and backend service logic

This fixed ownership keeps race analysis simple.

### Benefits

- Clear direction ownership
- Works naturally with DMA circular RX
- Works naturally with DMA burst TX
- Easy to add per-port overflow and watermark counters
- Easy to reason about backpressure per direction
- Fits the current 6-port architecture cleanly

### Costs

- Two buffers per port instead of one
- One copy is still required when CDC OUT data enters the TX ring
- Ring wrap handling is needed when launching DMA TX spans

### Recommended Buffer Shape

Per port:

- RX ring: 1024 bytes
- TX ring: 1024 bytes

For 6 ports, 1024-byte RX and TX rings use about 12 KB total.

### RX Implementation Detail

Use a circular DMA buffer for RX.

- DMA writes continuously into the ring
- software tracks the DMA write position
- software consumes from a separate read pointer

This is the best fit for asynchronous UART receive traffic.

### TX Implementation Detail

Use a software ring plus DMA bursts.

- USB pushes into the TX ring
- when TX DMA is idle, launch one contiguous span
- on completion, advance the consumer pointer and launch the next span if needed

This is the best fit for bursty host traffic and slower UART drain rates.

## Design B: Shared Memory Pool With RX And TX Descriptors

Each logical port still has separate RX and TX queues logically, but actual storage comes from a shared memory pool.
Instead of fixed rings, the firmware manages variable-sized blocks and descriptors.

### Data Flow

For USB to UART:

1. TinyUSB writes packets into free blocks from the pool.
2. TX descriptors queue those blocks to the UART backend.
3. TX DMA drains one descriptor block at a time.

For UART to USB:

1. RX DMA writes into blocks reserved from the pool.
2. RX descriptors queue completed blocks to TinyUSB.
3. TinyUSB drains completed blocks to the host.

### Benefits

- Better global memory sharing under uneven traffic
- Can reduce wasted reserved capacity on idle ports
- Can represent packet boundaries explicitly
- Useful if future firmware adds framing or protocol-aware processing

### Costs

- More complex allocator and descriptor handling
- Harder to make lock-free and race-free
- Harder DMA restart logic
- Harder overflow and ownership debugging
- More metadata churn and pointer chasing

### When It Makes Sense

Use this design only if:

- memory pressure becomes a real problem
- per-port fixed rings waste too much RAM
- packetized processing becomes a primary firmware feature

For the current UART bridge goal, this is probably over-engineered.

## Recommendation

Use Design A.

Why:

- it matches the USB CDC to UART bridge problem directly
- it keeps per-direction ownership clear
- it works well with DMA RX rings and DMA TX bursts
- it is easier to validate before adding multicore or more advanced scheduling

Design B should be kept only as a future option if the project later needs richer packet management or more aggressive memory sharing.

## End-To-End Architecture

Each logical port is modeled as one bridge instance.

Each bridge instance owns:

- one TX ring for USB-to-UART traffic
- one RX ring for UART-to-USB traffic
- one UART backend binding
- TX DMA state
- RX DMA state
- counters and status flags for monitoring

The current code already uses this model across both the hardware UART and PIO UART backends, and the USB CDC layer now drives the bridge end to end.

### Top-Level Blocks

1. TinyUSB CDC layer
2. USB-to-UART bridge layer
3. Ring-buffer layer
4. UART backend layer
5. DMA engine
6. HID monitor layer

### Responsibility Split

TinyUSB CDC layer:

- reads host OUT data
- writes host IN data
- owns line-coding and line-state callbacks

Bridge layer:

- maps CDC index to logical UART port
- moves data between TinyUSB and the correct TX or RX ring
- enforces backpressure policy
- does not touch UART or PIO hardware directly

Ring-buffer layer:

- provides fixed-size single-producer single-consumer queues
- exposes contiguous readable and writable spans
- maintains occupancy, high-water mark, and overflow counters

UART backend layer:

- owns hardware UART or PIO UART specifics
- owns DMA setup and DMA completion handling
- updates producer or consumer positions for the port rings
- keeps PIO RX interrupt-driven and lets the worker core choose between FIFO polling and TX DMA based on queue depth

## Current Scope Limits

The current transport deliberately targets a narrow UART subset:

- 8 data bits
- no parity
- 1 stop bit
- framing validation limited to stop-bit-high checks on the PIO RX path

This keeps the PIO and bridge logic compact while the multi-port data path is stabilized first.

HID monitor layer:

- snapshots counters and flags from each port
- publishes health and status to the host

## Per-Port Bridge State

Each logical port should eventually expose a structure conceptually equivalent to:

```c
typedef struct {
	uart_port_id_t port_id;
	uart_driver_backend_t backend;

	ring_buffer_t tx_ring;
	ring_buffer_t rx_ring;

	uint8_t tx_storage[1024];
	uint8_t rx_storage[1024];

	uint8_t tx_service_mode;
	uint32_t tx_dma_bytes_in_flight;
	uint32_t rx_dma_last_write_offset;

	uint32_t tx_reject_count;
	uint32_t rx_overwrite_count;
	uint32_t usb_out_bytes;
	uint32_t usb_in_bytes;
	uint32_t uart_tx_bytes;
	uint32_t uart_rx_bytes;
} uart_bridge_port_t;
```

The exact implementation can differ, but the ownership model and counters should remain equivalent.

The current hardware UART backend already contains the subset below:

- `ring_buffer_t tx_ring`
- `ring_buffer_t rx_ring`
- `tx_storage[1024]`
- `rx_storage[1024]`
- `tx_dma_active`
- `tx_dma_bytes_in_flight`

## End-To-End Data Flow

### Path A: Host To UART

This path handles USB CDC OUT traffic.

1. Host sends bytes to CDC `n`.
2. TinyUSB reports bytes available on CDC `n`.
3. Bridge layer reads bytes from TinyUSB.
4. Bridge layer writes those bytes into port `n` TX ring.
5. If the TX ring does not have enough space, the bridge stops accepting more bytes for now and records a TX-side rejection event.
6. If the backend TX DMA is idle, the backend reads the next contiguous TX span.
7. Backend launches a TX DMA transfer from that span into the UART data register or PIO TX FIFO feed path.
8. On DMA completion, the backend commits consumed bytes.
9. If more data remains in the TX ring, the backend launches the next contiguous span.

### Path B: UART To Host

This path handles UART RX traffic.

1. UART RX DMA writes incoming bytes into the port RX ring storage.
2. Backend observes DMA progress and updates the RX producer position.
3. Bridge layer checks whether TinyUSB CDC IN is writable.
4. Bridge layer requests the next contiguous readable RX span.
5. Bridge layer writes that span to the matching CDC IN endpoint.
6. After a successful TinyUSB write, the bridge commits consumed RX bytes.
7. If more readable data remains and TinyUSB can accept it, the bridge repeats the send.

### Path C: HID Status Monitoring

1. HID poll task snapshots per-port counters.
2. HID report includes backend type, pin map, high-water marks, overflow counts, and DMA-active flags.
3. Host monitor uses HID data to detect congestion or stalled flows.

## Execution Model

The design assumes a single TinyUSB owner.

TinyUSB-facing work must run in one execution context only.
That context may be the main loop or one dedicated core, but TinyUSB APIs should not be called from multiple cores.

Recommended single-core phase-1 order:

1. `tud_task()`
2. drain all CDC OUT endpoints into TX rings
3. kick idle UART TX DMA channels
4. sample UART RX DMA progress and update RX producers
5. drain RX rings into CDC IN endpoints
6. publish HID status if due

## Ring Semantics

Each ring is single-producer single-consumer.

TX ring:

- producer: bridge layer on behalf of USB CDC OUT
- consumer: UART backend TX DMA side

RX ring:

- producer: UART backend RX DMA side
- consumer: bridge layer on behalf of USB CDC IN

The generic ring helper should not perform blocking waits and should not know about TinyUSB or DMA.

## Concrete Buffer Sizing

Default per port:

- TX ring: 1024 bytes
- RX ring: 1024 bytes

Sizing rationale:

- large enough for several USB full-speed packets per port
- small enough to keep 6-port RAM use reasonable
- power-of-two size simplifies wrap handling

Optional future tuning:

- enlarge RX rings if targets stream faster than the host drains
- enlarge TX rings if host bursts are larger than UART drain windows

## RX Policy

RX uses DMA circular writes into the RX storage.

Selected policy on RX overrun:

- preserve newest data
- advance consumer to make space
- increment overflow counters

Reason:

- this is a live bridge, not a guaranteed archival logger
- stale unread bytes are less valuable than fresh stream visibility

This behavior should be explicit in documentation and visible in HID status.

## TX Policy

TX uses software enqueue and DMA burst drain.

Selected policy on TX full:

- reject bytes that do not fit
- increment TX reject counter
- do not silently discard previously queued data

Reason:

- queued host-originated bytes should remain ordered
- the host can retry if software exposes backpressure properly

## API Shape

The ring helper should provide:

- initialize
- reset
- occupancy
- free space
- readable contiguous span
- writable contiguous span
- commit produced bytes
- commit consumed bytes
- publish externally-produced RX progress
- bulk write helper
- bulk read helper
- high-water mark
- overflow count

The current implementation provides this concrete API:

```c
bool ring_buffer_size_is_valid(size_t size);
bool ring_buffer_init(ring_buffer_t *ring, uint8_t *storage, size_t size);
void ring_buffer_reset(ring_buffer_t *ring);
size_t ring_buffer_occupancy(const ring_buffer_t *ring);
size_t ring_buffer_free_space(const ring_buffer_t *ring);
ring_buffer_span_t ring_buffer_read_span(const ring_buffer_t *ring);
ring_buffer_span_t ring_buffer_write_span(ring_buffer_t *ring);
bool ring_buffer_commit_produced(ring_buffer_t *ring, size_t count);
bool ring_buffer_commit_consumed(ring_buffer_t *ring, size_t count);
void ring_buffer_publish_producer(ring_buffer_t *ring, size_t producer, bool preserve_newest);
size_t ring_buffer_write(ring_buffer_t *ring, const uint8_t *data, size_t length);
size_t ring_buffer_read(ring_buffer_t *ring, uint8_t *data, size_t length);
size_t ring_buffer_high_watermark(const ring_buffer_t *ring);
size_t ring_buffer_overflow_count(const ring_buffer_t *ring);
```

The bridge layer should provide per-port operations conceptually equivalent to:

```c
bool uart_bridge_port_init(uart_bridge_port_t *port);
void uart_bridge_port_poll_usb_out(uart_bridge_port_t *port, uint8_t cdc_index);
void uart_bridge_port_kick_tx(uart_bridge_port_t *port);
void uart_bridge_port_poll_rx(uart_bridge_port_t *port);
void uart_bridge_port_flush_usb_in(uart_bridge_port_t *port, uint8_t cdc_index);
void uart_bridge_port_snapshot_status(uart_bridge_port_t *port, uart_bridge_status_t *status);
```

These do not need to be implemented as exact function names, but the design should preserve these responsibilities.

## DMA Interaction Details

RX DMA:

- configured in circular mode where supported
- updates producer position from hardware write offset
- never copies into a second software queue

Current hardware UART implementation detail:

- RX DMA runs with a 1024-byte ring buffer
- software derives an absolute producer count from DMA transfer progress
- `ring_buffer_publish_producer(..., true)` is used so RX overflow preserves newest data

TX DMA:

- launched on one contiguous ring span at a time
- marks the TX service state as DMA-active while bytes are in flight
- commits consumed bytes only after transfer completion
- immediately launches another span if pending data remains

Current hardware UART implementation detail:

- TX DMA reads directly from the contiguous readable span returned by `ring_buffer_read_span`
- after DMA completion, the backend commits exactly `tx_dma_bytes_in_flight`
- if more data remains in the TX ring, the next contiguous span is launched immediately

If a backend cannot DMA directly from the ring storage, that backend may use a bounce buffer, but only at the backend layer.

## HID Status Fields

The HID monitor should eventually expose at least:

- backend type
- TX pin and RX pin
- TX ring occupancy
- RX ring occupancy
- TX high-water mark
- RX high-water mark
- TX reject count
- RX overflow count
- TX DMA active flag
- RX overrun flag

This makes the ring-buffer behavior measurable from the host.

Current HID status implementation does not expose these counters yet.
That is a planned follow-up, not current behavior.

## Failure Modes To Design For

- host sends faster than UART drains
- target sends faster than host drains
- one port is saturated while others are mostly idle
- TX ring wraps while DMA launch is pending
- RX ring overruns while USB is stalled
- HID polling must not disturb bridge throughput

The implementation should remain correct under all of these conditions.

## Phase Plan

Phase 1:

- generic fixed-size ring helper
- hardware UART backend using TX and RX rings
- overflow and watermark counters inside the helper

Phase 2:

- USB CDC bridge layer feeding TX rings and draining RX rings
- HID status fields extended for buffer metrics

Phase 3:

- 4 PIO UART ports reuse the same ring model
- optional multicore backend servicing if measurements justify it

## Integration Notes

Place the implementation under `firmware/src/ring_buffer`.

Expected usage:

- `driver/hw_uart_driver.c` uses RX and TX ring helpers for hardware UART DMA paths
- `driver/pio_uart_driver.c` uses the same helpers for PIO UART DMA paths
- `usb/usb_cdc.c` reads from and writes to rings through the bridge layer


## Bring-Up Plan

1. Implement the generic fixed-size ring helper.
2. Integrate it into the hardware UART backend.
3. Validate RX overflow handling, TX rejection behavior, and DMA restart logic.
4. Add the USB CDC bridge layer that uses the rings directly.
5. Add HID counters for ring health.
6. Reuse the same ring model for the 4 PIO UART ports.