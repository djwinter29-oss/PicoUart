# PicoUart .NET Host

**Monitor six UARTs from your terminal, test runner, or browser.** The .NET host provides a diagnostics CLI and local
dashboard for PicoUart on Raspberry Pi Pico and Pico 2.

Diagnostics use a separate USB HID interface, so your existing serial tools can keep using the six USB CDC ports.
This application does not capture UART payloads or set baud rates and framing; use your serial tools for those tasks.

## Requirements

- A computer matching one of the release targets below; the release archive needs no installed .NET runtime
- A Raspberry Pi Pico or Pico 2 running compatible
  [PicoUart firmware](https://github.com/djwinter29-oss/PicoUart/releases), connected over USB
- Access to the board's HID interface; Linux may need
  [device permissions](https://github.com/djwinter29-oss/PicoUart/blob/main/tools/README.md#linux-hid-permissions)

## Install

### Install from GitHub Releases

Download a self-contained host archive from
[GitHub Releases](https://github.com/djwinter29-oss/PicoUart/releases). Choose the target for the computer running the
application, not the Pico:

| Computer | Release Target | Archive |
| --- | --- | --- |
| Linux x64 | `linux-x64` | `.tar.gz` |
| Windows x64 | `win-x64` | `.zip` |
| Apple Silicon Mac | `osx-arm64` | `.tar.gz` |

Extract the **whole archive**, keeping its `web` directory and other files beside the executable. No .NET SDK, runtime,
or Python installation is needed.

### Install as a NuGet .NET tool

`PicoUart.DotNet` is a .NET global tool for the CLI and dashboard, **not a C# library**. Check the
[distribution channels](https://github.com/djwinter29-oss/PicoUart/blob/main/docs/deployment/host-app-publishing.md#distribution-channels)
for its current availability; until it is listed there, use the self-contained archives above. Unlike the archives,
the tool needs the .NET 10 SDK to install and the .NET 10 and ASP.NET Core runtimes to run.

Install and start the tool:

```sh
dotnet tool install --global PicoUart.DotNet
pico-uart-dotnet status
pico-uart-dotnet web
```

Update it with:

```sh
dotnet tool update --global PicoUart.DotNet
```

Remove it with:

```sh
dotnet tool uninstall --global PicoUart.DotNet
```

## Quick Start

For a NuGet tool installation, use `pico-uart-dotnet` instead of `./PicoUart` or `PicoUart.exe` in the archive
examples below; the same commands and options apply.

For an archive installation, connect the flashed board and run from the extracted directory on Linux/macOS:

```sh
./PicoUart status
./PicoUart web
```

On Windows PowerShell, use `.\PicoUart.exe status` or `.\PicoUart.exe web`. The remaining archive examples use the
Linux/macOS spelling; substitute `.\PicoUart.exe` on Windows.

Open <http://127.0.0.1:5000/>. The dashboard binds to loopback, needs no browser WebHID support, and shows channel
health and traffic, dropped-byte counts, firmware and hardware identity, clock, and temperature. Stop it with
**Ctrl+C**; it is a local server, not a system service. If the port is occupied, run `./PicoUart web --port 5001`.

For browser-only diagnostics without installing a host application, use the separate
[Direct Dashboard on GitHub Pages](https://djwinter29-oss.github.io/PicoUart/) in desktop Chrome or Edge, with the board
connected to the computer running that browser.

For multiple boards, use `./PicoUart --serial ABC123 status` or `./PicoUart --device-path PATH`, but not both. Ambiguous
matches are rejected. In the dashboard, use **Board** and **Rescan**. Run one host process per physical board;
separate processes do not share its input reports or traffic totals.

## Test Automation

Save a status snapshot, launch a timed JSON-lines monitor alongside a serial test, or collect cumulative dropped-byte
counts independently:

```sh
./PicoUart status > uart-status.json
./PicoUart monitor --duration 60 > uart-traffic.jsonl
./PicoUart overruns > uart-dropped-bytes.json
```

- `status` waits up to `--timeout` seconds (default 2) and exits non-zero without a valid report. It returns after the
  first valid channel report, so other channels may still be waiting placeholders.
- `monitor` prints one raw channel snapshot (`id` 0-5) per line with its `sequence`, measured `interval_ms`, and byte
  counters since MCU boot (modulo `2^32`). It does not compute rates or gaps, and it can finish successfully without
  receiving any reports. Samples completing at or after the deadline are excluded.
- `overruns` returns `null` for a channel whose dropped-byte count is unavailable. Dropped-byte counts and buffer peaks
  are cumulative since boot, so compare before and after a run.

Timeouts start after the device is found and validated. **A zero exit code is not a health check**: your test runner
must check that samples arrived and that the channels are healthy. Field meanings are defined in the
[HID report reference](https://github.com/djwinter29-oss/PicoUart/blob/main/docs/design/usb/hid-report-reference.md).

## Other Commands And Controls

Read-only commands include `hardware`, `config`, `telemetry`, `temperature`, and
`version`; `overruns` reads dropped-byte counts. Results are JSON unless the command reports a plain value. Run
`./PicoUart --help` for all commands.

`toggle-led` changes the board LED. `reset` reboots the board and requires firmware reset support, enabled by default.
The dashboard's **Reboot** button requests a standalone reboot; **Save & Reboot** saves a persistent boot-time clock
profile and then reboots. Stop UART traffic and close CDC ports before saving. After reconnecting, check the stored
profile and the reported system clock separately; a verified save does not prove the overclock is stable.

Controls validate device identity and capabilities, serialize requests, and verify terminal results. Commands have
three-second transfer/result deadlines and are never automatically retried after an uncertain outcome. Reconnect and
inspect board state before retrying. Save completion means flash verification succeeded and reboot was scheduled, not
that the next boot succeeded.

## Troubleshooting

- **Board not found:** check compatible firmware, the USB data cable, and any board selector. USB must be accessible to
  the computer running the host. Without a compatible board, HID commands failing is expected.
- **Access denied:** follow the
  [Linux HID permission guide](https://github.com/djwinter29-oss/PicoUart/blob/main/tools/README.md#linux-hid-permissions).
  After using an older Python/libusb client, close it and reconnect the board to restore HID discovery.
- **No valid reports:** check firmware compatibility and the USB connection. Missing reports are not zero traffic;
  verify that monitor output contains samples even when the command exits successfully.
- **Windows `unsupported report size or layout version` with matching v0.10.0 host and firmware:** use a host build
  containing the descriptor-padding fix; reflashing the board is not required.
- **Missing web files:** re-extract the complete archive; do not move only the executable.
- **`pico-uart-dotnet` not found after installation:** check that the global tool directory is on `PATH`
  (`$HOME/.dotnet/tools` on Linux/macOS or `%USERPROFILE%\.dotnet\tools` on Windows), then restart the shell.
- **Missing .NET runtime:** run `dotnet --list-runtimes` and install the .NET 10 SDK, which includes the required .NET
  and ASP.NET Core runtimes. See
  [Microsoft's .NET tool troubleshooting guide](https://learn.microsoft.com/en-us/dotnet/core/tools/troubleshoot-usage-issues).
- **Need framework logs:** set `Logging__LogLevel__Microsoft=Information` before launching. Startup normally prints one
  dashboard URL while retaining warnings and errors.

Saving configuration reboots the board. Close all CDC ports and stop UART traffic first; review the
[save safety contract](https://github.com/djwinter29-oss/PicoUart/blob/main/docs/design/usb/hid-report-reference.md#save-and-reboot-safety).

## Documentation And Feedback

- [Project, firmware, and wiring](https://github.com/djwinter29-oss/PicoUart)
- [Host development and source-checkout setup](https://github.com/djwinter29-oss/PicoUart/blob/main/docs/development/host-tools.md#net-development)
- [HID report reference and compatibility](https://github.com/djwinter29-oss/PicoUart/blob/main/docs/design/usb/hid-report-reference.md)
- [Security policy and trust model](https://github.com/djwinter29-oss/PicoUart/blob/main/SECURITY.md)
- [Report an issue or request a feature](https://github.com/djwinter29-oss/PicoUart/issues)
- [BSD 3-Clause License](https://github.com/djwinter29-oss/PicoUart/blob/main/LICENSE)
