# Control Plane Design

The control plane carries CDC line-coding requests (baud rate and framing) from TinyUSB on core 0 to the UART backend
on core 1, and reports the outcome through HID. It never carries UART bytes.

## The Core Problem

TinyUSB acknowledges `SET_LINE_CODING` before the firmware can safely apply it. Applying a new format mid-frame, or
while old-format bytes are still queued, would corrupt traffic. So the firmware:

1. accepts the request immediately and blocks new TX bytes for that port,
2. lets bytes queued under the old format drain,
3. applies the change on core 1 once the backend is idle, and
4. reports the outcome through HID.

The USB transfer result therefore means "received", not "applied". Hosts watch two HID health bits:
`CONTROL_PENDING` (bit 3) and `CONTROL_ERROR` (bit 2).

## Request Lifecycle

```mermaid
sequenceDiagram
  participant Host
  participant USB as "CDC / core 0"
  participant Mailbox as "Per-port mailbox"
  participant Worker as "UART worker / core 1"
  participant Backend as "UART backend"
  participant HID as "HID-visible status"

  Host->>USB: SET_LINE_CODING
  USB->>USB: Validate, mark pending
  USB->>HID: CONTROL_PENDING = 1
  USB->>Mailbox: Request + generation + TX boundary
  critical Under status lock
    Worker->>Mailbox: Take request
    Worker->>Worker: Become owner
  end
  Worker->>Worker: Wait for TX boundary and backend idle
  Worker->>Backend: Apply line coding
  Backend-->>Worker: Applied or rejected
  Worker->>HID: Update CONTROL_PENDING / CONTROL_ERROR
```

A request has three possible owners in turn: core 0 (soft-pending), the mailbox, and the worker. `CONTROL_PENDING` stays
set while any owner holds a request, and new TX bytes are refused for that whole interval. The hand-off between owners
happens under the status lock, so the request is never ownerless. Without this, a concurrent timeout could clear
pending while the worker was mid-change.

Malformed or unsupported requests are rejected at once with `CONTROL_ERROR`. Rejections tell the two causes apart:
invalid framing (for example, non-8N1 on a PIO port) and a baud rate the clock divider cannot reach.

## Ownership Rules

| Event | What happens |
| --- | --- |
| Valid request | Core 0 marks it pending and blocks new TX. On a closed port it waits without starting its timeout. |
| Mailbox submission | Settings, generation, and the TX boundary are published before core 0 releases its ownership. |
| Worker takes request | Taking the slot and becoming owner happen in one lock hold. |
| Apply | The worker drains to the TX boundary, waits for backend idle, and checks its deadline before touching hardware. |
| Completion or timeout | Only the completing owner is released. Pending clears when no owner remains. |
| Newer request rejected | Existing owners continue. An older valid apply may finish, but cannot erase the newer error. |
| DTR close or USB reset | Old-session requests are cancelled, the backend stops, and requests from a reopened session survive. |

Owners are independent: a newer request can wait while an older one is still being applied.

## Generations and Stale Completions

Each request increments a per-port generation, and every completion carries the generation it belongs to. A completion
may update `CONTROL_ERROR` only if its generation is still the latest. A slow, successful older request therefore cannot
clear the error from a newer rejected one.

## Timeouts

Soft-pending and worker application each have a bounded per-port deadline, so a busy port never consumes another port's
time. Repeating the same request does not extend its deadline, but a different request gets a new one. A timeout reports
`CONTROL_ERROR`.

## Port Lifecycle

DTR drives each port's lifecycle, and the UART worker is the only code that starts or stops a backend.

- **Open:** the initial line coding goes through the normal mailbox before the start request, so the backend starts with
  the requested format, not the previous session's. Each open makes one start attempt. On failure the port reports
  `INIT_FAILED` instead of retrying forever; close and reopen to retry.
- **Close:** core 0 stops admitting bytes and clears the port's CDC FIFOs. The worker then stops the backend and parks
  TX idle-high. Queued bytes are discarded, while the applied settings and boot-lifetime counters are kept. The board
  does not reboot.
- **`READY`:** means the backend is running and its reported settings match the hardware. Both directions are blocked
  until the port first becomes ready. During a later live reconfiguration, RX keeps draining and only TX is blocked.

One port's startup or close never holds up another port.

## USB Mount and Unmount Reset

Unplug, bus reset, and remount end the old USB session. The firmware clears host-facing CDC state, cancels soft-pending
requests without reporting them as timeouts, resets HID scheduling and reset-arm state, and stops the old session's
backends. A bus reset does not always fire TinyUSB's unmount callback, so the firmware also detects a lost mount by
polling.

## Host Guidance

- Opening a serial port does not guarantee the backend is running or the line coding is applied.
- After opening and configuring ports, wait on HID for `READY`, no `CONTROL_PENDING`, no current error, and the
  expected applied settings. The Python client's `wait_channels_ready()` does exactly this.
- Pending clearing on its own does not prove success; check the applied settings too.
- Clean serial data without clean HID health is incomplete test evidence.
