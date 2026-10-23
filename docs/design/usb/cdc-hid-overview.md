# CDC/HID Overview

PicoUart exposes one composite USB device with two host-facing interface types:

- six CDC ACM serial ports for UART data and UART line coding
- one vendor HID interface for status, diagnostics, and narrow board controls

Use CDC for normal serial traffic. Use HID to observe bridge health, pending or failed CDC control requests, and the
UART settings actually applied. USB transfer success alone does not establish successful reconfiguration.

## Interface Roles

| Interface  | Count | Purpose                                                                                    | Host tool                                    |
| ---------- | ----: | ------------------------------------------------------------------------------------------ | -------------------------------------------- |
| CDC ACM    |     6 | Serial data, DTR state, and CDC line-coding requests for UART0 through UART5               | Terminal programs, test runners, serial APIs |
| Vendor HID |     1 | Status, diagnostics, and narrow board controls                                            | Python/.NET; WebHID diagnostics              |

Each CDC port maps to one target-side UART channel. The GPIO pinout is defined in
[Hardware Setup](../../hardware-setup.md).

## CDC Behavior

Core 0 owns one state record per CDC interface: DTR and cumulative statistics, startup admission, deferred line coding,
close-flush progress, and the partial-IN flush deadline. Session cleanup updates only transient fields; it does not
zero the record or reset boot-lifetime counters. USB mount ownership and round-robin scheduling remain device-wide.

CDC carries serial bytes and `SET_LINE_CODING` requests for the matching UART. DTR controls the per-channel backend
lifecycle. On an open-to-closed transition, firmware clears that interface's CDC FIFOs and the UART worker
deinitializes the backend; queued bytes are discarded while applied line coding and boot-lifetime counters are
preserved. No UART bridge traffic is processed while the channel is closed. On the next open, the worker starts the
backend and applies any deferred line-coding request before bridging resumes. A USB packet already in flight may still
complete. Closed interfaces discard received OUT bytes on polling without repeatedly flushing an empty FIFO; the
explicit close/quiescence flush remains unconditional. Hardware UARTs support the broader validated line-coding set;
PIO UARTs are 8N1-only.

TinyUSB may complete a line-coding request before firmware applies or rejects it. Hosts must monitor HID health for the
result; request ownership and deferred application are described in [Control Plane Design](../control-plane-design.md).

The native [CDC tests](../../../firmware/tests/test_usb_cdc.c) exercise transport pressure and concurrent control
requests using production ring bridging with bounded USB FIFO and per-port mailbox mocks. TinyUSB's transfer-completion
flush and backend stop completion are modeled, not executed. Repeated pressure/recovery checks verify exact payloads,
counter deltas, and bounded progress across ring-storage wraps and channel close/reopen cycles.
USB scheduling and DMA timing still require the
[HIL plan](../../tests/hil-fixture-test-plan.md).

## HID Behavior

HID carries no UART data and cannot select UART pins or backends. It reports bridge health, board metadata, and applied
UART settings. Board controls include explicit LED On/Off, capability-gated reset, and persistent boot-time clock-profile
saving.
A verified configuration save automatically reboots the board, independently of the optional reset-command capability;
see the [save safety contract](hid-report-reference.md#save-and-reboot-safety) before using it.
Exact report layouts, health bits, and command semantics are defined in the
[HID Report Reference](hid-report-reference.md). The version-1 design uses report 1 for static device information,
report 2 for board commands and boot configuration, report 3 for board telemetry, and reports 10-15 for combined
per-channel traffic, settings, and compact fault history. Channel snapshots publish every 500 ms as Input reports;
the same layout is available through read-only Feature reads.

## Typical Host Workflow

Set UART baud and framing through CDC. Wait for HID pending state to settle, inspect control errors, and verify the
reported applied settings; pending state clearing alone is not proof of success. During traffic tests, monitor HID
health and traffic statistics alongside the CDC byte stream. Host commands and hardware acceptance criteria are
documented in the [Python host guide](../../../host/python/README.md) and
[HIL Fixture Test Plan](../../tests/hil-fixture-test-plan.md).
