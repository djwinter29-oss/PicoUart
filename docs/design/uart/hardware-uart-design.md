# Hardware UART Design

This document describes the hardware UART backend used for the two logical ports mapped to RP2040/RP2350 UART
peripherals. Shared adapter behavior is covered by [Backend Adapter Design](backend-adapter-design.md); generic ring
ownership is covered by [Ring Buffer Design](ring-buffer-design.md).

## Ownership

Core 0 owns TinyUSB and the CDC-side bridge. Core 1 owns PL011 registers, RX/TX DMA, flow-control GPIO, and deferred
line-coding application. The backend never calls TinyUSB; per-port rings are its data-plane boundary.

## Lifecycle and Resources

Initialization validates the peripheral and pin configuration, claims RX and TX DMA channels, configures the PL011, and
starts circular RX DMA. The backend holds its channels until deinitialization. Other DMA users must account for
resources reserved across all configured backends.

acquisition or initialization failure releases resources already claimed. The top-level driver rolls back initialized
ports and does not start the worker unless all configured ports are ready. If a runtime line-format change cannot be
restored safely, the backend is marked unavailable and reports `INIT_FAILED` rather than continuing on a stopped
UART.

## RX Path

Circular DMA writes UART bytes into aligned per-port storage. The shared IRQ handler re-arms exhausted transfers, with
worker polling as a fallback. The backend publishes DMA progress to the RX ring; the bridge validates copied spans
against live DMA progress and rejects overwritten snapshots so loss can be accounted for.

## TX Path

The worker sends bounded contiguous TX-ring spans by DMA when the UART is idle and no control transition blocks TX. It
commits bytes only after transfer completion. Span length is limited by ring contiguity, the configured maximum, and a
wire-time budget so one busy port cannot monopolize the worker.

## Hardware Flow Control

Hardware RTS/CTS is opt-in through the board configuration. When enabled:

- CTS is configured as active-low UART flow control
- RTS is driven through a SIO GPIO output
- RTS uses occupancy hysteresis
- RTS deasserts when RX occupancy reaches the high watermark
- RTS reasserts after occupancy falls to the low watermark

The default board configuration keeps hardware flow control disabled. Pin assignment alone does not advertise a lossless
flow-control guarantee.

## Baud Rate and Line Coding

The adapter checks PL011 divisor representability and rejects rates exceeding the configured error tolerance. Supported
data bits, stop bits, and parity are validated before application.

A line-coding change is deferred until the old TX boundary drains and the UART is idle. The backend pauses RX DMA,
publishes stable progress, applies the supported format, then restarts DMA at the live producer position so unread data
is preserved. If application fails, it restores the previous format; failure to restore disables the backend. Request
ownership, timeout, and HID status behavior are specified in [Control Plane Design](../control-plane-design.md).

## Deinitialization and Error Baseline

Deinitialization publishes final RX DMA progress before disabling its IRQ, aborting and releasing DMA channels, and
deinitializing the UART. GPIO functions are released during backend cleanup.

After successful startup, the top-level driver clears receive-status errors collected during bring-up. Runtime framing
and receive-status errors then remain visible through backend statistics and HID health reporting.

## Limits and Validation

- Hardware UART line coding depends on PL011 divisor precision and the configured baud error tolerance.
- RX overflow remains possible when the peer ignores RTS or the host does not drain CDC IN quickly enough.
- DMA, IRQ timing, UART idle behavior, and RTS/CTS behavior require
    [hardware-in-the-loop testing](../../tests/hil-fixture-test-plan.md).
- Host tests cover pure baud/policy and resource-claim logic; the real backend path is validated by firmware builds and
  the documented hardware test plans.
