# PicoUart Direct Dashboard

A browser dashboard for the [PicoUart](https://github.com/djwinter29-oss/PicoUart) six-channel USB-to-UART bridge. It
shows board identity, runtime health, per-channel traffic, and RX dropped-byte counts for Pico and Pico 2, with nothing
to install.

It talks to the board's USB HID interface through the browser's WebHID API, with no Python or .NET backend. UART data
and baud/framing settings stay on the six CDC serial ports.

## Requirements

- Desktop Chrome or Edge with WebHID. Firefox, Safari, and most mobile browsers do not support it.
- A Pico or Pico 2 running [PicoUart firmware](https://github.com/djwinter29-oss/PicoUart/releases), connected to the
  computer running the browser
- Operating-system access to the HID interface; on Linux, set up
  [device permissions](../../tools/README.md#linux-hid-permissions)

## Use It

Open the [hosted dashboard](https://djwinter29-oss.github.io/PicoUart/), connect the flashed board with a USB data
cable, click **Connect**, and choose the PicoUart HID interface. The button becomes **Disconnect** while connected.
Pages you host yourself need HTTPS or localhost.

Identity is read once, and the channel snapshots then refresh continuously. Fault history and buffer peaks are read
separately as diagnostics. After you unplug the board or it reboots, click **Connect** again; the page never reopens a
device on its own. Missing or unreadable values show `--` with the read error, and the firmware must speak the
[current HID report format](../../docs/design/usb/hid-report-reference.md).

Run one HID monitor per board, because other native clients compete for the same reports. If your browser lacks WebHID,
use the [Python](../python/README.md) or [.NET](../dotnet/README.md) dashboard instead.

## Test Automation

This is an interactive page, not a test runner: no CLI, no JSON export, and no UART capture. For scripted status and
traffic logs, use the [Python CLI](../python/README.md#command-line) or the
[.NET CLI](../dotnet/README.md#test-automation), and disconnect the browser first.

Traffic figures are cumulative byte counters. If a report is missed, the rate estimate is marked incomplete for the rest
of the connection; that does not mean bytes were lost.

## Controls

The dashboard can turn the identification LED on or off and reboot the board when the firmware supports it. Boot
configuration saves a boot-time clock profile and then reboots, so USB briefly disconnects. All six channels must be
Closed with fresh reports, and non-default clocks need the unsafe-overclock confirmation. Close all CDC ports and stop
UART traffic first. An uncertain save is remembered in the browser session; if that storage is unavailable, saving is
disabled.

After a save, check the stored profile and the running clock. The
[save and reboot rules](../../docs/design/usb/hid-report-reference.md#save-and-reboot-safety) cover the details. The
page writes nothing else, and it never exposes credentials or raw flash.

## Troubleshooting

- **Connect is disabled:** use desktop Chrome or Edge on HTTPS or localhost, and check that policy allows WebHID.
- **Board not found or access denied:** check the firmware, the USB data cable, and the browser's site permissions.
  Browser permission does not replace OS permissions, so close other HID clients too.
- **Page is remote:** the browser chooser lists USB devices on the computer running the browser. A page forwarded from
  an SSH or WSL workspace does not forward USB. Plug the board into the browser's computer, or use the Python or .NET
  dashboard where the board is attached. Never type an OS password into the page.

## Development

Source is in `src/` and checks are in `tests/`; serve or publish `src/` directly, with no build step. See
[Dashboard Development](../../docs/development/dashboard-development.md#direct-dashboard), the
[project README](../../README.md), and the [security policy](../../SECURITY.md).
