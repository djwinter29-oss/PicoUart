# PicoUart WebHID Dashboard

**Monitor six UARTs directly from your browser.** The WebHID dashboard shows PicoUart identity, runtime health,
per-channel traffic statistics, and RX dropped-byte counts on Raspberry Pi Pico and Pico 2.

Diagnostics use a separate USB HID interface, without a Python/.NET application backend. UART payloads and baud/framing
settings still use the six USB CDC serial ports, not this page.

## Requirements

- Desktop Google Chrome or Microsoft Edge with WebHID enabled; Firefox and Safari are not supported, and mobile
	browsers generally do not support WebHID
- A Raspberry Pi Pico or Pico 2 running compatible
	[PicoUart firmware](https://github.com/djwinter29-oss/PicoUart/releases), connected to the computer running the browser
- Browser permission and OS access to the board's HID interface; Linux may need
	[device permissions](../../tools/README.md#linux-hid-permissions)

## Install

Open the [hosted WebHID dashboard](https://djwinter29-oss.github.io/PicoUart/). No local installation, Python, .NET,
or server is required. The GitHub Pages site uses HTTPS; self-hosted pages also require HTTPS or localhost.
If your browser does not support WebHID, use the local [Python](../python/README.md) or [.NET](../dotnet/README.md)
dashboard instead.

## Quick Start

1. Connect the flashed board to this computer over a USB data cable.
2. Open the hosted page, click **Connect**, and choose the PicoUart vendor HID interface.
3. Inspect board identity, runtime values, and UART health/traffic alongside your serial tools.

The button becomes **Disconnect** while connected. Metadata/runtime refresh every second. Disconnect closes the HID
connection; unplugging clears the last sample. After unplug/replug or a configuration-save reboot, use **Connect**
again; the app does not automatically select or reopen a device.

Firmware must implement the current version-1 HID contract. Missing or invalid reports show `--` with an explicit
read error; no alternate layouts are supported.
Read failures leave the device available for automatic polling or Disconnect, but clear stale metadata rather than
inventing a clock or MCU.

Use one HID monitor per physical board; competing native clients can contend for input reports or device access.

## Test Automation

This page is an interactive diagnostic view, not a headless test runner. It has no CLI, JSON/log export, or UART
payload capture. For scripted status samples and traffic logs, use the [Python CLI](../python/README.md#test-automation)
or [.NET CLI](../dotnet/README.md#test-automation). Disconnect the browser before another client monitors the same
board.

Traffic statistics are not raw UART messages. Missing reports or saturated byte deltas make observed totals incomplete;
a quiet or healthy-looking page alone is not evidence that an automated test passed.

## Other Commands And Controls

**Board LED** switches board identification on/off; its indicator shows physical output. Older firmware may also use
that output for USB activity.
The **Persistent configuration** section saves the selected boot-time clock profile. A verified save automatically
reboots the board and briefly disconnects USB, even though there is no standalone reset command. Close all CDC ports
and stop external UART traffic first; non-default clocks require explicit unsafe-overclock acknowledgement.

Persistent configuration source, fallback reason, and active/stored clock profiles are read from report 16; this
diagnostic does not expose credentials or raw flash contents.
After reconnecting, inspect the actual active profile and any fallback reason rather than assuming the requested clock
became active. A verified save does not establish overclock stability. See the
[configuration procedure](../../docs/development/host-tools.md#persistent-configuration).

Writes are limited to user-initiated LED toggle and configuration save. Firmware flashing and WebSerial payload access
are not implemented.

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

- [Project, firmware, and wiring](../../README.md)
- [Host development and source-checkout setup](../../docs/development/host-tools.md#webhid-development)
- [HID report reference and compatibility](../../docs/design/usb/hid-report-reference.md)
- [Security and lab-only USB identity](../../SECURITY.md)