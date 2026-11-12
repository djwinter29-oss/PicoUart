# PicoUart Direct Dashboard

**Monitor six UARTs directly from your browser over USB.** The Direct Dashboard shows PicoUart identity, runtime health,
per-channel traffic statistics, and RX dropped-byte counts on Raspberry Pi Pico and Pico 2.

Diagnostics use the browser's WebHID API and a separate USB HID interface, without a Python/.NET application backend.
UART payloads and baud/framing
settings still use the six USB CDC serial ports, not this page.

## Requirements

- Desktop Google Chrome or Microsoft Edge with WebHID enabled; Firefox and Safari are not supported, and mobile
  browsers generally do not support WebHID
- A Raspberry Pi Pico or Pico 2 running compatible
  [PicoUart firmware](https://github.com/djwinter29-oss/PicoUart/releases), connected to the computer running the browser
- Browser permission and OS access to the board's HID interface; Linux may need
  [device permissions](../../tools/README.md#linux-hid-permissions)

## Install

Open the [hosted Direct Dashboard](https://djwinter29-oss.github.io/PicoUart/). No local installation, Python, .NET,
or server is required. The GitHub Pages site uses HTTPS; self-hosted pages also require HTTPS or localhost.
If your browser does not support WebHID, use the local [Python](../python/README.md) or [.NET](../dotnet/README.md)
dashboard instead.

## Quick Start

1. Connect the flashed board to this computer over a USB data cable.
2. Open the hosted page, click **Connect**, and choose the PicoUart vendor HID interface.
3. Inspect board identity, runtime values, and UART health/traffic alongside your serial tools.

The button becomes **Disconnect** while connected. Static device information is read once; channel snapshots cover
traffic, applied settings, and channel state. Fault history and buffer peaks are read separately through diagnostics.
Disconnect closes the HID connection; unplugging clears the last sample. After unplug/replug or a configuration-save
reboot, use **Connect** again; the app does not automatically select or reopen a device.

Firmware must implement the [current HID report contract](../../docs/design/usb/hid-report-reference.md). Missing or
invalid reports show `--` with an explicit read error; no alternate layouts are supported.
Read failures leave the device available for automatic polling or Disconnect, but clear stale metadata rather than
inventing a clock or MCU.

Use one HID monitor per physical board; competing native clients can contend for input reports or device access.

## Test Automation

This page is an interactive diagnostic view, not a headless test runner. It has no CLI, JSON/log export, or UART
payload capture. For scripted status samples and traffic logs, use the
[Python CLI](../python/README.md#command-line-tools) or [.NET CLI](../dotnet/README.md#test-automation). Disconnect
the browser before another client monitors the same board.

Traffic statistics are cumulative byte counters, not raw UART messages. A missing report can make throughput estimates
incomplete without confirming byte loss. Counter wrap can make an increment unknowable if at least `2^32` bytes pass
between samples. A quiet or healthy-looking page alone is not evidence that an automated test passed. The continuity
notice signals incomplete observation, not confirmed byte loss, and remains for the current connection. See the
[HID report reference](../../docs/design/usb/hid-report-reference.md) for counter and measurement semantics.

## Other Commands And Controls

**Board LED** sends explicit identification On/Off commands; its indicator shows physical output.
The capability-gated **Reboot board** control requests a standalone reboot. The **Boot configuration** section saves
the selected boot-time clock profile; a verified save automatically reboots the board and briefly disconnects USB.
Saving requires all six channels to be Closed with fresh Input reports; the confirmation is disabled if those reports
expire. Close all CDC ports and stop external UART traffic first; non-default clocks require explicit unsafe-overclock
acknowledgement. The dashboard records an uncertain save in browser session storage. If recovery storage is
unavailable, monitoring and other supported controls remain available, but configuration saves are disabled.

After reconnecting, inspect the stored boot profile request and running clock separately; do not infer the requested
profile from the clock or assume it became active. The dashboard does not report a configuration fallback reason or
active profile. A verified save does not establish overclock stability. See the
[HID report reference](../../docs/design/usb/hid-report-reference.md) and
[configuration procedure](../../docs/development/host-tools.md#persistent-configuration). This diagnostic does not
expose credentials or raw flash contents.

Writes are limited to user-initiated LED state changes, standalone reboot, and configuration save. Firmware flashing
and WebSerial payload access are not implemented.

## Troubleshooting

- **Connect disabled:** use desktop Chrome/Edge in a secure context and check whether browser policy disables WebHID.
- **Board not found:** check compatible firmware, the USB data cable, device enumeration, and browser site permissions.
- **Access denied:** browser permission does not replace OS permissions; follow the Linux permission guide above and
  close competing HID clients.
- **No valid reports:** check firmware compatibility and USB connectivity. Missing values are not zero traffic.

WebHID does not request a password. Chrome's chooser lists eligible USB devices connected to the computer running
Chrome. Forwarding a page from a remote Linux/WSL/SSH workspace does not forward its USB devices.

If the Pico is attached to the remote machine, use the [Python](../python/README.md) or [.NET](../dotnet/README.md)
dashboard on its forwarded, configured local port, or connect the USB data cable to the browser's computer. If it is
already local, check the USB data cable, device enumeration, browser site permissions, and OS device permissions.
Installing an OS access rule may require administrator approval in the OS; do not enter that password into the page.

## Documentation

- Source assets live in `src/`; automated checks live in `tests/`. Serve or publish only `src/`, with no build step.
- [Project, firmware, and wiring](../../README.md)
- [Host development and source-checkout setup](../../docs/development/host-tools.md#direct-dashboard-development)
- [HID report reference and compatibility](../../docs/design/usb/hid-report-reference.md)
- [Vulnerability reporting](../../SECURITY.md)