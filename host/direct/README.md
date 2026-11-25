# PicoUart Direct Dashboard

A browser dashboard for the [PicoUart](https://github.com/djwinter29-oss/PicoUart) six-channel USB-to-UART bridge. Open
it in desktop Chrome or Edge to see board identity, runtime health, per-channel traffic, and RX dropped-byte counts for
Pico and Pico 2. Nothing to install:

**<https://djwinter29-oss.github.io/PicoUart/>**

It talks to the board's USB HID interface through the browser's WebHID API, so no Python or .NET backend is involved.
UART data and baud/framing settings stay on the six CDC serial ports.

## Requirements

- Desktop Chrome or Edge with WebHID. Firefox, Safari, and most mobile browsers do not support it.
- A flashed board ([firmware releases](https://github.com/djwinter29-oss/PicoUart/releases)) connected to the computer
  running the browser
- Operating-system access to the HID interface; on Linux, follow the
  [device permission setup](../../docs/hardware-setup.md#linux-hid-permissions)
- Firmware with the [current HID report format](../../docs/design/usb/hid-report-reference.md)

## Use It

Connect the board with a USB data cable, open the [hosted dashboard](https://djwinter29-oss.github.io/PicoUart/),
click **Connect**, and choose the PicoUart HID interface. The button becomes **Disconnect** while connected. Pages you
host yourself need HTTPS or localhost.

Identity is read once, and the channel snapshots then refresh continuously. Fault history and buffer peaks are read
separately as diagnostics. Traffic figures are cumulative byte counters; if a report is missed, the rate estimate is
marked incomplete for the rest of the connection.

Run one HID monitor per board, because other native clients compete for the same reports.

For scripted logs, JSON export, or a browser without WebHID, disconnect the browser and use the
[Python](../python/README.md) or [.NET](../dotnet/README.md) host.

## Controls

The dashboard can turn the identification LED on or off and reboot the board when the firmware supports it. Boot
configuration saves a boot-time clock profile and then reboots, so USB briefly disconnects. Close all CDC ports and
stop UART traffic first; saving is enabled only when fresh reports show all six channels Closed. Non-default clocks
need the unsafe-overclock confirmation. An uncertain save is remembered in the browser session; if that storage is
unavailable, saving is disabled.

After a save, check the stored profile and the running clock. The
[save and reboot rules](../../docs/design/usb/hid-report-reference.md#save-and-reboot-safety) cover the details. The
page writes nothing else, and it never exposes credentials or raw flash.

## Troubleshooting

- **Connect is disabled:** use desktop Chrome or Edge on HTTPS or localhost, and check that policy allows WebHID.
- **Board not found or access denied:** check the firmware, the USB data cable, and the browser's site permissions.
  Browser permission does not replace OS permissions, so close other HID clients too.
- **A value shows `--`:** inspect its read error and update the firmware if it does not use the current HID report
  format.
- **Board unplugged or rebooted:** click **Connect** again; the page does not reopen a device automatically.
- **Page is remote:** the browser chooser lists USB devices on the computer running the browser. A page forwarded from
  an SSH or WSL workspace does not forward USB. Plug the board into the browser's computer, or use the Python or .NET
  dashboard where the board is attached. Never type an OS password into the page.

## Development

Source is in `src/` and checks are in `tests/`; serve or publish `src/` directly, with no build step. See the
[dashboard behavior and presentation guide](../../docs/development/dashboard-development.md#direct-dashboard), the
[project README](../../README.md), and the [security policy](../../SECURITY.md).
