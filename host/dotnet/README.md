# PicoUart .NET Host

A command-line tool and local dashboard for the [PicoUart](https://github.com/djwinter29-oss/PicoUart) six-channel
USB-to-UART bridge on Raspberry Pi Pico and Pico 2. It reads channel health and traffic over the board's USB HID
interface, so your serial tools keep using the six CDC ports. It does not capture UART data or set baud rates.

## Requirements

- Linux x64, Windows x64, or Apple Silicon macOS (the release archive needs no .NET install)
- A Pico or Pico 2 running [PicoUart firmware](https://github.com/djwinter29-oss/PicoUart/releases), connected by USB
- Access to the board's HID interface; on Linux, set up
  [device permissions](https://github.com/djwinter29-oss/PicoUart/blob/main/tools/README.md#linux-hid-permissions)

## Install

### From a release archive

Download the archive for your computer (not the Pico) from
[GitHub Releases](https://github.com/djwinter29-oss/PicoUart/releases):

| Computer | Target | Archive |
| --- | --- | --- |
| Linux x64 | `linux-x64` | `.tar.gz` |
| Windows x64 | `win-x64` | `.zip` |
| Apple Silicon Mac | `osx-arm64` | `.tar.gz` |

Extract the whole archive and keep the `web` folder next to the executable.

### As a .NET tool

`PicoUart.DotNet` is also a .NET global tool for the CLI and dashboard. It is not a C# library. Check the
[distribution channels](https://github.com/djwinter29-oss/PicoUart/blob/main/docs/deployment/host-app-publishing.md#distribution-channels)
to see whether it is currently published. It needs the .NET 10 SDK to install and the .NET 10 and ASP.NET Core
runtimes to run.

```sh
dotnet tool install --global PicoUart.DotNet
dotnet tool update --global PicoUart.DotNet
dotnet tool uninstall --global PicoUart.DotNet
```

## Quick Start

The examples use the archive spelling. For the .NET tool, use `pico-uart-dotnet`; on Windows, use `.\PicoUart.exe`.

```sh
./PicoUart status
./PicoUart web
```

Open <http://127.0.0.1:5000/>. The dashboard listens on loopback only, needs no WebHID, and shows channel health and
traffic, dropped bytes, firmware and hardware identity, clock, and temperature. Press `Ctrl+C` to stop it, and use
`--port 5001` if the port is taken.

With several boards, use `--serial ABC123` or `--device-path PATH` (not both), or pick one in the dashboard with
**Board** and **Rescan**. Run one host process per board.

No install at all? The [Direct Dashboard](https://djwinter29-oss.github.io/PicoUart/) runs in desktop Chrome or Edge.

## Test Automation

```sh
./PicoUart status > uart-status.json
./PicoUart monitor --duration 60 > uart-traffic.jsonl
./PicoUart overruns > uart-dropped-bytes.json
```

- `status` waits up to `--timeout` seconds (default 2) and exits non-zero if no valid report arrives. It returns after
  the first channel report, so other channels may still be placeholders.
- `monitor` prints one raw snapshot per line: `id` (0-5), `sequence`, `interval_ms`, and byte counters since boot. It
  does not compute rates, and it can exit successfully with no samples at all.
- `overruns` prints cumulative dropped-byte counts, or `null` where unavailable. Compare before and after a run.

Because a zero exit code does not mean the channels were healthy, have your test check that samples arrived and look
at the reported states. Field meanings are in the
[HID report reference](https://github.com/djwinter29-oss/PicoUart/blob/main/docs/design/usb/hid-report-reference.md).

## Other Commands

`hardware`, `config`, `telemetry`, `temperature`, and `version` are read-only; `./PicoUart --help` lists them all.
`toggle-led` flips the board LED, and `reset` reboots the board when the firmware allows it.

The dashboard's **Save & Reboot** stores a boot-time clock profile and then reboots. Stop UART traffic and close all CDC
ports first. After the board comes back, check the stored profile and the reported clock. Control commands are never
retried automatically; if the outcome is unclear, reconnect and check the board before trying again. The full rules
are in the
[save and reboot section](https://github.com/djwinter29-oss/PicoUart/blob/main/docs/design/usb/hid-report-reference.md#save-and-reboot-safety)
of the HID reference.

## Troubleshooting

- **Board not found / no reports:** check the firmware, the USB data cable, and any board selection. With no board
  attached, HID commands fail by design.
- **Access denied:** see the Linux permissions link above.
- **Missing web files:** re-extract the whole archive; the executable needs its `web` folder.
- **Windows "unsupported report size or layout version" with v0.10.0 host and firmware:** use a host build that includes
  the descriptor-padding fix. You do not need to reflash the board.
- **`pico-uart-dotnet` not found:** add the global tool directory to `PATH` (`$HOME/.dotnet/tools` on Linux/macOS,
  `%USERPROFILE%\.dotnet\tools` on Windows) and restart the shell.
- **Missing .NET runtime:** run `dotnet --list-runtimes`, and install the .NET 10 SDK if needed. See
  [Microsoft's troubleshooting guide](https://learn.microsoft.com/en-us/dotnet/core/tools/troubleshoot-usage-issues).
- **Framework logs:** set `Logging__LogLevel__Microsoft=Information` before launching.

## More

- [Project, firmware, and wiring](https://github.com/djwinter29-oss/PicoUart)
- [Developing from source](https://github.com/djwinter29-oss/PicoUart/blob/main/docs/development/host-tools.md#net-development)
- [Security policy](https://github.com/djwinter29-oss/PicoUart/blob/main/SECURITY.md)
- [Report an issue](https://github.com/djwinter29-oss/PicoUart/issues)
- [BSD 3-Clause License](https://github.com/djwinter29-oss/PicoUart/blob/main/LICENSE)
