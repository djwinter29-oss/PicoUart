# Architecture

PicoUart is a composite USB device with six CDC ACM serial ports and one vendor HID interface. Each CDC port is bridged
to one UART. UART0-1 run on the chip's hardware UARTs, and UART2-5 run on PIO state machines.

```mermaid
flowchart LR
  Host["Host"]
  subgraph Device["PicoUart"]
    CDC["6 CDC ports<br/>core 0"]
    Rings["Per-port RX/TX rings"]
    Backends["2 hardware + 4 PIO UARTs<br/>core 1"]
    HID["HID status and control<br/>core 0"]
    Control["Control plane"]
    CDC <-->|"Serial data"| Rings
    Rings <-->|"UART data"| Backends
    CDC -.->|"Line coding"| Control
    Control -.->|"Deferred apply"| Backends
    Control -.->|"Pending / error"| HID
    Backends -.->|"Telemetry"| HID
  end
  Target["Target devices"]

  Host <-->|"USB CDC"| CDC
  Host <-->|"USB HID"| HID
  Backends <-->|"UART0-5"| Target
```

Solid lines carry UART bytes. Dashed lines carry configuration and telemetry only. Baud rate and framing are set
via CDC; HID only reports what was applied.

## USB Interfaces

The composite USB device exposes six CDC ACM interfaces and one vendor HID interface:

| Interface | Count | Carries | Used by |
| --- | ---: | --- | --- |
| CDC ACM | 6 | UART bytes, DTR, and line coding for UART0-UART5 | Terminals, test runners, serial libraries |
| Vendor HID | 1 | Status, diagnostics, and board controls | Python, .NET, and Direct Dashboard |

Each CDC port maps to one UART; [hardware setup](../hardware-setup.md#port-map) owns the pin map. The
[control plane](control-plane-design.md) defines session and line-coding transitions, and
[ring buffers](uart/ring-buffer-design.md) define buffering and full-ring behavior. The
[HID reference](usb/hid-report-reference.md) specifies report layouts and board actions.

Use **CDC port** for the USB serial interface and **channel** for its firmware status and diagnostics; channel `n`
maps to CDCn.

## Design Principles

- **One core per side.** Core 0 owns TinyUSB and the USB side of the bridge. Core 1 owns all UART hardware. Neither
  core calls into the other's private state.
- **Rings are the data boundary.** Every port has one TX ring (USB to UART) and one RX ring (UART to USB). Each ring has
  exactly one producer and one consumer.
- **Configuration stays on the control path.** Core 0 hands line-coding requests to core 1, which applies them at a
  safe point; core 0 does not touch UART hardware.
- **Diagnostics are separate from data.** HID reports health and counters without competing with serial traffic.
- **Board pinout stays separate.** A constant board table maps each port to its pins and backend; transport and
  lifecycle code stay board-independent.

## Component Map

```mermaid
flowchart LR
  Port["Logical-port API"] -->|"Rings and snapshots"| Ops["Backend operations"]
  Runtime["UART runtime / worker"] -->|"Start, stop, service"| Ops
  Control["Control plane"] -->|"Validate and apply"| Ops
  Ops --> HW["Hardware UART backend"]
  Ops --> PIO["PIO UART backend"]
```

| Component | Responsibility | Design |
| --- | --- | --- |
| Rings | Per-port buffering, overflow and backpressure policy | [Ring buffers](uart/ring-buffer-design.md) |
| Control plane | Line-coding hand-off, deadlines, error reporting | [Control plane](control-plane-design.md) |
| Backend operations | Common contract for both UART implementations | [Backend contract](uart/backend-adapter-design.md) |
| Hardware UART | PL011 with RX/TX DMA | [Hardware UART](uart/hardware-uart-design.md) |
| PIO UART | PIO state machines with RX DMA and hybrid TX | [PIO UART](uart/pio-uart-design.md) |
| HID protocol | Report layouts, states, and board actions | [HID report reference](usb/hid-report-reference.md) |

## Inter-Core Coordination

Coordination uses a small set of mechanisms, each with a single job. They rely on RP2040/RP2350 guarantees (atomic
aligned 32-bit access and SDK memory barriers) rather than portable C11 threading.

### Ring Ownership

Rings are single-producer/single-consumer. The producer publishes payload before advancing its cursor, and the consumer
retires bytes only after reading them, with memory barriers between. Rings take no locks, so the data path never
waits on status reads.

### Control Mailbox

Each port has one mailbox slot for line-coding requests. Core 0 fills the slot under the shared status lock. Core 1
takes it and records that it now owns the request in the same lock hold, so there is never a moment when a request
has no owner. An empty mailbox therefore does not mean the change is done; core 1 may still be waiting for a safe apply
point. Ports never block each other's mailboxes.

### Shared State and Telemetry

The status lock protects lifecycle flags, control ownership, request generations, and applied settings, but not ring
cursors. Core 1 brackets each statistics update with an odd/even sequence counter; core 0 accepts a snapshot only if
the counter is even and unchanged across the read.

### Worker Heartbeat and Startup

Core 1 bumps a heartbeat after each full control-and-I/O pass. Core 0 feeds the watchdog only while the heartbeat keeps
moving, so a wedged UART worker triggers a reboot instead of failing silently.

Core 1 is explicitly reset before launch, so startup never depends on the state a debugger or earlier boot left behind.

## Constraints

- The six active ports use all 12 RP2040 DMA channels.
- All six ports share one USB full-speed link (12 Mbit/s). For example, six full-duplex 3 Mbaud streams would need
  3.6 MB/s, more than USB can carry. Baud rates configure each port; the shared link limits aggregate throughput.
- Multicore, DMA, and USB timing can only be proven on hardware; see the
  [HIL test plan](../tests/hil-fixture-test-plan.md).
