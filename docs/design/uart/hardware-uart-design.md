# Hardware UART Design

This document describes the hardware UART backend used for the two logical ports mapped to RP2040/RP2350 UART
peripherals. Shared backend behavior is covered by [Backend Contract Design](backend-adapter-design.md); generic ring
ownership is covered by [Ring Buffer Design](ring-buffer-design.md).

## Ownership

Startup initializes the configured backends before the UART worker is launched. During steady-state operation, core 0
owns TinyUSB and the CDC-side bridge; core 1 services PL011 registers, RX/TX DMA, flow-control GPIO, and deferred
line-coding application. The backend never calls TinyUSB; per-port rings are its data-plane boundary. Shared ownership
and synchronization rules are defined in [Architecture](../architecture.md#inter-core-coordination).

## Lifecycle and Resources

Initialization validates the peripheral and pin configuration, claims RX and TX DMA channels, configures the PL011, and
starts circular RX DMA. The backend holds its channels until deinitialization. Other DMA users must account for
resources reserved across all configured backends. Reinitializing an active backend is rejected before hardware startup,
preserving its DMA ownership, ring contents, and applied settings.

Acquisition or initialization failure releases resources already claimed. The top-level driver rolls back initialized
ports and does not start the worker unless all configured ports are ready. If a runtime line-format change cannot be
restored safely, the backend is marked unavailable and reports `INIT_FAILED` rather than continuing on a stopped
UART.

## RX Path

Circular DMA writes UART bytes into aligned per-port storage. The shared IRQ handler re-arms exhausted transfers, with
worker polling as a fallback. The backend publishes DMA progress to the RX ring; the bridge validates copied spans
against live DMA progress and rejects overwritten snapshots so loss can be accounted for.

## TX Path

The worker sends bounded contiguous TX-ring spans by DMA when the UART is idle and no control transition blocks TX. It
commits bytes only after transfer completion. Span length is limited by ring contiguity and a configured wire-time
budget so one busy port cannot monopolize the worker.

Completion accounting only commits the finished span; the outer poll owns the single TX launch attempt. Fixed DMA
width, increment, peripheral request, and destination settings are configured once per channel claim. Each launch
publishes ownership, sets the source address, restores channel `EN` without triggering, then triggers the transfer count.
Restoring `EN` is required because format-change aborts clear it, including when the previous format is restored.

DMA completion means the reserved bytes have been handed to the UART peripheral, not that the final stop bit has left
the TX pin. Safe line-coding application also requires UART idle; an empty ring or completed DMA alone is insufficient.

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

The native backend checks PL011 divisor representability and rejects rates exceeding the configured error tolerance.
Supported data bits, stop bits, and parity are validated before application.

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
- Host tests cover baud/policy, resource claims, and rejection of repeated initialization. A real-driver harness with SDK
  register-call mocks checks TX completion and launch ordering, fixed DMA reuse, RX pause/restart, and line-format success,
  rollback, and unrecoverable failure. These checks do not establish physical register timing or UART line behavior.
- Firmware builds validate SDK integration; the documented hardware test plans establish physical behavior for the
  tested artifacts and conditions.
