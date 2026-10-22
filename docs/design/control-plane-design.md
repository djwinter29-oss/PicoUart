# Control Plane Design

This document describes PicoUart's UART control plane: how CDC line-coding requests move from TinyUSB on core 0 to UART
backend reconfiguration on core 1, and how failures are reported through HID health.

The data plane is documented separately in [Ring Buffer Design](uart/ring-buffer-design.md) and
[PIO UART Design](uart/pio-uart-design.md). The host-facing CDC/HID relationship is summarized in
[CDC/HID Overview](usb/cdc-hid-overview.md).

## Scope

The control plane handles CDC line-coding requests, not UART bytes. It coordinates USB-side validation with deferred
per-port backend application and reports pending or failed requests through HID. UART bytes remain on the RX/TX rings.

## Request Lifecycle

Valid requests move from USB-side pending to the per-port mailbox, then to worker ownership until the backend reaches a
safe apply boundary. Invalid or unsupported formats set `CONTROL_ERROR`. The worker applies a request only after TX has
drained to its captured boundary and backend-specific quiescence is satisfied.

Mailbox acknowledgement and worker-ownership changes must be atomic under the shared status lock. Otherwise a concurrent
reject or timeout could clear `CONTROL_PENDING` after mailbox acknowledgement but before worker ownership is visible,
allowing TX during an active format change. The worker also checks its deadline immediately before touching the backend,
so a late apply cannot report success after timeout.
Memory ordering and cross-core synchronization are defined in
[Architecture: Inter-Core Coordination](architecture.md#inter-core-coordination).

```mermaid
sequenceDiagram
  participant Host
  participant USB as "CDC / core 0"
  participant Mailbox as "Per-port mailbox"
  participant Worker as "UART worker / core 1"
  participant Backend as "UART backend"
  participant HID as "Shared HID-visible status"

  Host->>USB: SET_LINE_CODING
  USB->>USB: Validate and mark soft-pending
  USB->>HID: CONTROL_PENDING = 1
  USB->>Mailbox: Publish request + generation + TX boundary
  critical Mailbox-to-worker handoff under status lock
    Worker->>Mailbox: Copy request and acknowledge slot
    Worker->>Worker: Retain or register worker ownership
  end
  Worker->>Worker: Validate backend support
  Worker->>Worker: Wait for TX boundary and backend idle
  Worker->>Backend: Apply line coding
  Backend-->>Worker: Applied or rejected
  Worker->>HID: Update CONTROL_PENDING / CONTROL_ERROR
```

CDC hosts cannot rely on the USB control transfer result alone. TinyUSB may have already completed the request before
firmware applies or rejects it. Hosts must watch HID health bit 2 (`control_error`) and bit 3 (`control_pending`).

## Ownership States

`CONTROL_PENDING` remains set while any of the soft-pending, mailbox, or worker owners remain. It blocks admission of
new USB-to-UART bytes throughout that interval. Already-queued TX bytes may drain toward the captured old-format
boundary so reconfiguration can proceed, but draining that boundary alone does not clear pending state or admit new
bytes. Clear it only after all owners have completed; a newer reject must not unblock TX while an older apply is active.

Mailbox acknowledgement releases the slot, not the worker's deferred apply. Backend reconfiguration can still be
pending after the mailbox becomes empty.

## Generations and Stale Completions

Each host control request increments a per-port generation. Completions carry the generation they belong to. A
completion may update `CONTROL_ERROR` only when its generation still equals the latest generation.

This prevents a stale successful completion from clearing an error reported for a newer rejected request. It also lets
an older valid request continue applying after a newer permanent reject; the older completion is no longer allowed to
erase the newer error.

## Timeouts

Soft-pending and worker application use separate bounded per-port deadlines; one busy port does not consume another
port's timeout. A valid request received while DTR is low or its backend is starting remains deferred without consuming
the mailbox timeout. That timeout starts when the open backend becomes ready to accept configuration. Repeating the
same eligible request does not extend its deadline indefinitely; a distinct replacement gets a new deadline. Timeout
reports `CONTROL_ERROR`; the configured duration is maintained in the control-plane implementation.

## USB Mount and Unmount Reset

USB mount and unmount clear host-facing CDC state, cancel soft-pending requests, and reset HID scheduling and reset-arm
state. Canceled requests must not later produce timeout errors. Separately, a CDC DTR open-to-closed edge requests a
per-port close: USB core clears that interface's CDC FIFOs and pauses its bridge, then the UART worker deinitializes
only that backend. Queued bytes are discarded; applied line coding and cumulative counters are preserved, and the board
does not reboot. On the next open, the worker starts that backend and applies any deferred line-coding request before
the bridge resumes.

Close invalidates older control generations. Worker close completion preserves control requests received after reopen,
and a start completion cannot overwrite a newer close/open request or clear a newer control error. Each open makes one
backend start attempt; failure publishes `INIT_FAILED` instead of retrying indefinitely and blocking other ports' control
processing. Close and reopen to retry. Startup is serviced on the next worker sweep without a fixed DTR debounce,
cross-channel barrier, or fixture-specific startup order. Closed backends park TX idle-high, including hardware-UART
configuration-failure cleanup. Control service skips channels without `READY`, and idle channels avoid a second lock
pass and telemetry sequence-counter updates. One channel's startup or close cannot hold another channel's configuration.

CDC callbacks remain asynchronous. POSIX TTY `open()` does not guarantee backend initialization or applied line coding.
After opening and configuring a group, hosts can use Python `wait_channels_ready()` on the matching HID device to wait
for `READY`, DTR open, no `CONTROL_PENDING`, no current initialization/configuration error, and applied 8N1 settings
within 3% of the requested baud. The wait uses a shared deadline (3 seconds by default), reports current errors
immediately, propagates transport failures, and raises `TimeoutError` if readiness or matching settings never arrive.
Historical fault counters are not current configuration failures and do not by themselves fail this handshake.

## Host-Visible Rules

- `SET_LINE_CODING` may look successful to the host even when firmware rejects the request later.
- `CONTROL_PENDING` means soft-pending, mailbox, or worker ownership has not yet settled.
- `CONTROL_ERROR` means the most recent host control request failed, timed out, or was unsupported.
- Data transfer tests should include HID monitoring; clean byte transfer without clean HID health is incomplete
  evidence.

## Test Coverage

Host tests cover mailbox ownership, deadlines, stale completions, and TX blocking. CDC integration tests execute the
production callbacks with simulated time and UART/TinyUSB calls: delayed closed configuration, startup readiness,
identical retries, replacement requests, close/reopen, disconnect, and stale errors. These checks do not establish real
USB bus behavior, backend quiescence, or physical timing under load; those require hardware validation. See the
[HIL fixture plan](../tests/hil-fixture-test-plan.md).
