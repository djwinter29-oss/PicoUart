# CDC/HID Overview

PicoUart exposes one composite USB device with two host-facing interface types:

- six CDC ACM serial ports for UART data and UART line coding
- one vendor HID interface for status, diagnostics, and narrow board controls

Use CDC for normal serial traffic. Use HID to observe whether the bridge is
healthy and whether a CDC control request was accepted by firmware.

## Interface Roles

| Interface | Count | Purpose | Host tool |
| --- | ---: | --- | --- |
| CDC ACM | 6 | Serial data, DTR state, and CDC line-coding requests for UART0 through UART5 | Terminal programs, test runners, serial APIs |
| Vendor HID | 1 | Status reports, firmware version, temperature, overflow counts, LED toggle, optional reset | `host/python/src/pico_uart_hid.py` |

Each CDC port maps to one target-side UART channel. The GPIO pinout is defined
in [UART Pinout and Wiring](../uart-pinout.md).

## CDC Behavior

CDC ports carry bytes between the host and the target UARTs:

- CDC OUT data is queued into the matching UART TX ring.
- UART RX data is drained from the matching RX ring into CDC IN.
- CDC DTR is recorded for HID status only; it does not gate bridging.
- CDC line-coding requests are parsed and queued to the matching UART backend.

Hardware UART ports accept the broader supported CDC line-coding set. PIO UART
ports are 8N1-only and reject unsupported parity, stop-bit, or data-bit changes.

TinyUSB may complete a CDC `SET_LINE_CODING` transfer before firmware can apply
or reject it. Hosts must use HID health to see the firmware result.

## HID Behavior

The HID interface does not carry UART data and does not select UART pins or
backends. It exposes board and bridge state:

- periodic per-channel health and traffic reports
- firmware version and board temperature
- cumulative UART-to-USB RX overflow counts
- board LED toggle
- optional arm-then-reset command when compiled into trusted lab firmware

Unsupported CDC line coding, deferred-apply timeout, or backend reject appears
as the per-channel HID `control_error` health bit. RX overflow and UART framing
errors are also reported through HID health and overflow queries.

The detailed HID report layout is documented in
[HID Report Reference](hid-report-reference.md).

## Typical Host Workflow

1. Open the desired CDC ports and run serial traffic or tests.
2. Use `pico_uart_hid.py monitor` during bring-up or stress tests to watch
   `control_error`, `rx_overrun`, and `rx_error` health bits.
3. Use `pico_uart_hid.py overruns` before and after a test to verify RX overflow
   counts did not increase.
4. Treat a clean serial byte stream without clean HID health as incomplete test
   evidence.

For hardware test commands and acceptance criteria, use
[Functional Test Plan](../tests/functional-test-plan.md) and
[Performance Test Plan](../tests/performance-test-plan.md).