# USB Interfaces

PicoUart enumerates as one composite USB device with two kinds of interface:

| Interface | Count | Carries | Used by |
| --- | ---: | --- | --- |
| CDC ACM | 6 | UART bytes, DTR, and line coding for UART0-UART5 | Terminals, test runners, serial libraries |
| Vendor HID | 1 | Status, diagnostics, and a few board controls | Python, .NET, and Direct Dashboard |

Use CDC for serial traffic and HID to see what the bridge is doing. Keeping them separate means monitoring never takes
over a serial port, and a stuck serial client never hides the board's health. The pin map is in
[hardware setup](../../hardware-setup.md).

## CDC Ports

Each CDC port behaves like an ordinary USB serial adapter for one UART. DTR opens and closes the UART; while a port is
closed nothing is bridged and received bytes are discarded. The port's buffering and full-ring behavior are described
in [Ring Buffer Design](../uart/ring-buffer-design.md).

Hardware-UART ports accept the usual CDC framing options. PIO ports accept 8N1 only.

The firmware keeps one state record per CDC port for DTR, startup progress, and cumulative counters. Closing a session
clears the transient state but keeps the boot-lifetime counters.

## HID Interface

HID carries no UART data and cannot change pins or backends. A host reads identity and configuration once (firmware
version, board, capabilities, stored clock profile), then follows live telemetry (system clock, temperature, core load)
and channel snapshots about every 500 ms with channel state, traffic counters, and applied settings. Channel diagnostics
provide fault history, dropped-byte counts, and buffer peaks on demand. The only board actions are the identification
LED, an optional reboot, and saving the boot-time clock profile.

Saving the clock profile always reboots the board, so close all serial ports first. Report layouts and action rules
are in the [HID report reference](hid-report-reference.md).
