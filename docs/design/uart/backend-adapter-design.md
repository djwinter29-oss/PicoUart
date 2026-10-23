# Backend Operations Contract

## Purpose

PicoUart supports two UART implementations behind one runtime orchestration layer:

hardware UART with DMA and PIO UART with state machines and DMA.

Both drivers implement one operations contract directly. Each owns a constant operations table; there is no forwarding
adapter implementation. The UART runtime, port facade, and control plane use that contract without backend-specific
branches. It is an internal boundary; USB and board modules do not call it directly.

This document describes stable responsibilities and data ownership, not a promise about private function names, callback
signatures, or file-local helper names. Those implementation details may change as long as the operation contract and
ownership rules remain intact.

## Shared Contract

The contract exposes backend lifecycle and readiness, worker I/O, per-port rings, line-coding support/application, RX
snapshot validation, and telemetry. This contract describes operation semantics rather than callback signatures;
ring ownership belongs to [Ring Buffer Design](ring-buffer-design.md), and reconfiguration ordering belongs to
[Control Plane Design](../control-plane-design.md).

An incomplete operation table or unknown backend selection fails safely. Callback contexts refer to the selected
driver's runtime storage. The native entry points have matching signatures, without function-pointer casts or wrapper
dispatch. Concrete backends own hardware resources, parity conversion, baud matching, and counter reporting.

Both backends reject malformed or clock-infeasible requests before hardware application. Matching requires an
initialized instance. Setters stage valid settings on a bound stopped instance without touching hardware, or safely
apply them to an active instance. Configuration binding refuses active-instance rebinding and accepts only the selected backend's
immutable board-table entry. Failed binding leaves the existing configuration and applied settings unchanged.
Null-context queries return neutral results: false readiness/matching, null rings, zero baud, and zero counters.
Non-null contexts must be correctly typed instances for the selected backend. Queries may inspect inactive state;
callers must check readiness before interpreting that state as live telemetry.

## Lifecycle and Ownership

The board configuration selects the backend. The top-level driver owns its runtime state and lifecycle and selects
the driver's operations table. Each backend owns its registers, DMA channels or PIO state machines, and rings. USB
code uses the public UART interface rather than backend state.

Backend descriptors are constant tables with compile-time pointers to the common board records. GPIO assignments are
read through those pointers, not copied or assembled by runtime configuration builders. Applied baud and framing live
in mutable driver state because host line-coding requests can change them without modifying the startup defaults.
Static topology is checked by host tests; peripheral initialization and DMA/state-machine claims still run at startup.

Startup is all-or-nothing: if any configured backend fails, initialized backends are rolled back and the worker does not
start. Runtime operations reject unavailable backends rather than accessing partially initialized state.

The logical-port owner supplies fixed aligned RX storage with the same lifetime as the backend. Its allocation is
separate from runtime metadata to avoid propagating DMA alignment through backend and port structs; the
[ring-buffer design](ring-buffer-design.md#capacity) owns the storage contract. The `restart` operation stops and starts
hardware and discards queued bytes; it is not just a software-buffer clear.

Each DTR-open start seeds the complete initial baud/framing from its mailbox request, or from the last successfully
applied metadata when no request is queued. Failed startup does not change applied metadata. A later retry reseeds all
fields rather than inheriting unpublished settings left in the stopped backend; reported framing must match hardware
whenever lifecycle `READY` is published.

Traffic/error counters belong to the lifetime of the zero-initialized backend storage, not one hardware start.
Restart clears only transient hardware state and queued ring bytes/reservations. Ring watermark and overflow counts
are preserved by `ring_buffer_clear()` after producer, consumer, and DMA quiesce; cold `ring_buffer_init()` resets
those counters. Backend restart therefore does not need to save and restore telemetry field by field.

## Line Coding

Each driver distinguishes formats it supports from whether a supported change is currently safe to apply. The
control plane owns request ordering, TX boundaries, timeout, and HID status; backend-specific quiescing and application
belong to the concrete implementation. See [Control Plane Design](../control-plane-design.md).

Both setters accept the same backend-neutral line-coding record. Hardware UART translates parity into SDK values and
compares applied baud rates using its hardware divider. PIO remains 8N1 and rejects other framing; it accepts only baud
changes that are representable by its clock divider.

## Validation

Host tests call the real driver-owned tables and native setters, covering malformed requests, parity translation,
applied-baud matching, PIO framing/divider limits, rings, and counter reporting. Only hardware-facing lifecycle and
application functions are stubbed in those tests; register/DMA sequencing is not simulated by these assertions.
The same tests cover null/invalid inputs, real static-descriptor binding, and refusal to reset active instances.
Separate tests cover operation-table completeness and the static topology. Firmware builds compile and link both
backends. Hardware timing, PIO FIFO behavior, USB traffic, and physical wiring require
[hardware-in-the-loop testing](../../tests/hil-fixture-test-plan.md).
