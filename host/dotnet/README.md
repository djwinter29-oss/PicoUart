# PicoUart .NET Host

A command-line tool and local dashboard for the [PicoUart](https://github.com/djwinter29-oss/PicoUart) six-channel
USB-to-UART bridge. Download, extract, and run `./PicoUart web`; no .NET install is needed. It reads channel health
and traffic over the board's USB HID interface, so your serial tools keep using the six CDC ports. UART data capture
and baud-rate changes belong to those serial tools.

## Requirements

- Linux x64, Windows x64, or Apple Silicon macOS
- A flashed board ([firmware releases](https://github.com/djwinter29-oss/PicoUart/releases)) connected by USB
- Access to the board's HID interface; on Linux, follow the
  [device permission setup](https://github.com/djwinter29-oss/PicoUart/blob/main/docs/hardware-setup.md#linux-hid-permissions)

## Install

Download the archive for your computer (not the Pico) from
[GitHub Releases](https://github.com/djwinter29-oss/PicoUart/releases):

| Computer | Target | Archive |
| --- | --- | --- |
| Linux x64 | `linux-x64` | `.tar.gz` |
| Windows x64 | `win-x64` | `.zip` |
| Apple Silicon Mac | `osx-arm64` | `.tar.gz` |

Extract the whole archive and keep the `web` folder next to the executable.

## Quick Start

On Windows, use `.\PicoUart.exe`.

```sh
./PicoUart status
./PicoUart web
```

Open <http://127.0.0.1:5000/>. The dashboard listens on loopback only, works in any browser, and shows channel health
and traffic, dropped bytes, firmware and hardware identity, clock, and temperature. Press `Ctrl+C` to stop it, and
use `--port 5001` if the port is taken.

With several boards, use `--serial ABC123` or `--device-path PATH` (not both), or pick one in the dashboard with
**Board** and **Rescan**. Run one host process per board.

No install at all? The [Direct Dashboard](https://djwinter29-oss.github.io/PicoUart/) runs in desktop Chrome or Edge.

## Test Automation

```sh
./PicoUart status > uart-status.json
./PicoUart monitor --duration 60 > uart-traffic.jsonl
./PicoUart overruns > uart-dropped-bytes.json
```

| Command | Prints | Exits `1` when |
| --- | --- | --- |
| `status` | One JSON snapshot after the first channel report; other channels may still be placeholders | No valid report arrives within `--timeout` seconds (default 2) |
| `monitor` | One raw JSON snapshot per line: `id` (0-5), `sequence`, `interval_ms`, byte counters since boot; no rates | The board cannot be read; a run with no samples still exits `0` |
| `overruns` | Cumulative dropped-byte counts, `null` where unavailable; compare before and after a run | The board cannot be read |

Exit code `0` does not mean the channels were healthy: check that samples arrived and look at the reported states.
Field meanings are in the
[HID report reference](https://github.com/djwinter29-oss/PicoUart/blob/main/docs/design/usb/hid-report-reference.md).

## Other Commands

`hardware`, `config`, `telemetry`, `temperature`, and `version` are read-only; `./PicoUart --help` lists them all.
`toggle-led` flips the board LED, and `reset` reboots the board when the firmware allows it.

**Save & Reboot** stores a boot-time clock profile and restarts the board, so stop UART traffic and close all CDC ports
first. The dashboard does not retry an uncertain control; check the board before trying again.

## Troubleshooting

- **Board not found / no reports:** check the firmware, the USB data cable, and any board selection. With no board
  attached, HID commands fail by design.
- **Access denied:** see the Linux permissions link above.
- **Missing web files:** re-extract the whole archive; the executable needs its `web` folder.
- **Older host reports an unsupported report size or layout:** update the host application to the latest release.
- **Framework logs:** set `Logging__LogLevel__Microsoft=Information` before launching.

## More

- [Project, firmware, and wiring](https://github.com/djwinter29-oss/PicoUart)
- [Developing from source](https://github.com/djwinter29-oss/PicoUart/blob/main/docs/development/host-tools.md#net-development)
- [Security policy](https://github.com/djwinter29-oss/PicoUart/blob/main/SECURITY.md)
- [Report an issue](https://github.com/djwinter29-oss/PicoUart/issues)
- [BSD 3-Clause License](https://github.com/djwinter29-oss/PicoUart/blob/main/LICENSE)
