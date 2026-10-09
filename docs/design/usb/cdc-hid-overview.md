# CDC/HID Overview

PicoUart exposes one composite USB device with two host-facing interface types:

- six CDC ACM serial ports for UART data and UART line coding
- one vendor HID interface for status, diagnostics, and narrow board controls

Use CDC for normal serial traffic. Use HID to observe whether the bridge is healthy and whether a CDC control request
was accepted by firmware.

## Interface Roles

| Interface  | Count | Purpose                                                                                    | Host tool                                    |
| ---------- | ----: | ------------------------------------------------------------------------------------------ | -------------------------------------------- |
| CDC ACM    |     6 | Serial data, DTR state, and CDC line-coding requests for UART0 through UART5               | Terminal programs, test runners, serial APIs |
| Vendor HID |     1 | Status, diagnostics, and narrow board controls                                            | Python/.NET; WebHID diagnostics              |

Each CDC port maps to one target-side UART channel. The GPIO pinout is defined in
[UART Pinout and Wiring](../../uart-pinout.md).

## CDC Behavior

CDC carries serial bytes and `SET_LINE_CODING` requests for the matching UART. DTR is reported for monitoring but does
not gate data transfer. Hardware UARTs support the broader validated line-coding set; PIO UARTs are 8N1-only.

TinyUSB may complete a line-coding request before firmware applies or rejects it. Hosts must monitor HID health for the
result; request ownership and deferred application are described in [Control Plane Design](../control-plane-design.md).

## HID Behavior

HID carries no UART data and cannot select UART pins or backends. It reports bridge health and board metadata, with
narrowly scoped board controls. Exact report layouts, health bits, and command semantics are defined in the
[HID Report Reference](hid-report-reference.md).

## Typical Host Workflow

During bring-up and testing, check HID health alongside the CDC byte stream. Host commands and hardware acceptance
criteria are documented in the [Python host guide](../../../host/python/README.md) and
[HIL Fixture Test Plan](../../tests/hil-fixture-test-plan.md).
