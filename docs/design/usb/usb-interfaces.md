# USB Interfaces

PicoUart enumerates as one composite USB device with two kinds of interface:

| Interface | Count | Carries | Used by |
| --- | ---: | --- | --- |
| CDC ACM | 6 | UART bytes, DTR, and line coding for UART0-UART5 | Terminals, test runners, serial libraries |
| Vendor HID | 1 | Status, diagnostics, and a few board controls | Python, .NET, and Direct Dashboard |

Use CDC for serial traffic and HID to see what the bridge is doing. Keeping them separate means monitoring never takes
over a serial port, and a stuck serial client never hides the board's health. The pin map is in
[hardware setup](../../hardware-setup.md).

## Using Both Together

1. Open the serial port and set the baud rate and framing through CDC.
2. Watch HID until the port is `READY` with no pending or failed control request, and confirm the applied settings.
3. Run traffic over CDC while monitoring HID health and counters.

## CDC Ports

Each CDC port behaves like an ordinary USB serial adapter for one UART:

Bytes flow through the port's [rings](../uart/ring-buffer-design.md). When the TX ring is full the firmware stops
reading that port, so USB pushes back on the host instead of dropping data. DTR opens and closes the UART; while a port
is closed nothing is bridged and received bytes are discarded. Line coding is applied later on core 1, so a successful
USB transfer does not mean the change took effect. The result shows up in HID health (see the
[control plane](../control-plane-design.md)).

Hardware-UART ports accept the usual CDC framing options. PIO ports accept 8N1 only.

The firmware keeps one state record per CDC port for DTR, startup progress, and cumulative counters. Closing a session
clears the transient state but keeps the boot-lifetime counters.

## HID Interface

HID carries no UART data and cannot change pins or backends. A host reads identity and configuration once (firmware
version, board, capabilities, stored clock profile), then follows live telemetry (system clock, temperature, core load)
and channel snapshots about every 500 ms with traffic counters, health bits, and applied settings per port. Fault
history, dropped-byte counts, and buffer peaks are available on demand. The only board actions are the identification
LED, an optional reboot, and saving the boot-time clock profile.

Saving the clock profile always reboots the board, so close all serial ports first. Report layouts and action rules
are in the [HID report reference](hid-report-reference.md).
