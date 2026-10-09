# Backend Adapter Design

## Purpose

PicoUart supports two UART implementations behind one runtime orchestration layer:

hardware UART with DMA and PIO UART with state machines and DMA.

The adapter keeps backend details private while allowing the UART runtime, port facade, and control plane to use one
contract. It is an internal boundary; USB and board modules do not call it directly.

This document describes stable responsibilities and data ownership, not a promise about private function names, callback
signatures, or file-local helper names. Those implementation details may change as long as the operation contract and
ownership rules remain intact.

## Shared Contract

The adapter exposes backend lifecycle and readiness, worker I/O, per-port rings, line-coding support/application, RX
snapshot validation, and telemetry. The exact operation table is declared in
[adapter.h](../../../firmware/src/uart/backend/adapter.h); keep callback signatures there rather than duplicating them
in this overview.

An incomplete operation table or unknown backend selection fails safely. The adapter dispatches and translates types;
concrete backends own hardware resources and backend-specific behavior.

## Lifecycle and Ownership

The board configuration selects the backend. The top-level driver owns its runtime state and lifecycle; the adapter
provides dispatch, while each concrete backend owns its registers, DMA channels or PIO state machines, and rings. USB
code uses the public UART interface rather than backend state.

Startup is all-or-nothing: if any configured backend fails, initialized backends are rolled back and the worker does not
start. Runtime operations reject unavailable backends rather than accessing partially initialized state.

## Line Coding

The adapter distinguishes formats a backend supports from whether a supported change is currently safe to apply. The
control plane owns request ordering, TX boundaries, timeout, and HID status; backend-specific quiescing and application
belong to the concrete implementation. See [Control Plane Design](../control-plane-design.md).

Hardware UART supports the broader validated CDC format set. PIO remains 8N1 and accepts only baud changes that are
representable by its clock divider.

## Validation

Host tests cover operation-table completeness; firmware builds compile and link both backends. Hardware timing, PIO FIFO
behavior, USB traffic, and physical wiring require [hardware-in-the-loop testing](../../tests/hil-fixture-test-plan.md).
