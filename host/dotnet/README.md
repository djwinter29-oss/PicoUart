# PicoUart .NET Host

**Monitor six UARTs from your terminal, test runner, or browser.** The .NET host provides a diagnostics CLI, local
dashboard, and C# HID client for PicoUart on Raspberry Pi Pico and Pico 2.

Diagnostics use a separate USB HID interface, so your existing serial tools can keep using the six USB CDC ports.
This application does not capture UART payloads or set baud rates and framing; use your serial tools for those tasks.

## Install From A Release

Download a self-contained host archive from [GitHub Releases](https://github.com/djwinter29-oss/PicoUart/releases).
Choose the target for the computer running the application, not the Pico:

| Computer | Release Target | Archive |
| --- | --- | --- |
| Linux x64 | `linux-x64` | `.tar.gz` |
| Windows x64 | `win-x64` | `.zip` |
| Apple Silicon Mac | `osx-arm64` | `.tar.gz` |

Extract the **whole archive**, keeping its `web` directory and other files beside the executable. No .NET SDK, runtime,
or Python installation is needed. See [distribution details](../../docs/development/host-distribution.md#net-downloads)
for supported downloads and packaging limitations.

Connect a board running compatible [PicoUart firmware](https://github.com/djwinter29-oss/PicoUart/releases) over USB to
the computer running the application. Linux may need [HID permissions](../../tools/README.md#linux-hid-permissions).

## Quick Start

From the extracted directory on Linux/macOS:

```sh
./PicoUart status
./PicoUart web
```

On Windows PowerShell, use `.\PicoUart.exe status` or `.\PicoUart.exe web` instead. The remaining CLI examples use the
Linux/macOS executable spelling; substitute `.\PicoUart.exe` on Windows.

Open <http://127.0.0.1:5000/>. The dashboard binds to loopback, works without browser WebHID support, and shows channel
health and traffic, dropped-byte counts, firmware and hardware identity, clock, and temperature. Use
`./PicoUart web --port 5001` if the default port is occupied. This starts a local server, not a system service.

For browser-only diagnostics without installing a host application, use the separate
[WebHID dashboard on GitHub Pages](https://djwinter29-oss.github.io/PicoUart/) in desktop Chrome or Edge, with the board
connected to the computer running that browser.

For multiple boards, use `./PicoUart --serial ABC123 status` or `--device-path PATH`, but not both. Ambiguous matches
are rejected. Use **Board** and **Rescan** in the dashboard; see
[board selection and shared monitoring](../../docs/development/host-tools.md#board-selection-and-shared-monitoring).

## Test Automation

Status and monitor samples are JSON by default; `--json` is accepted but unnecessary. Have your test runner save a
snapshot, launch a timed monitor alongside a serial test, or collect cumulative RX dropped-byte counts independently:

```sh
./PicoUart status > uart-status.json
./PicoUart monitor --duration 60 > uart-traffic.jsonl
./PicoUart overruns > uart-dropped-bytes.json
```

Each monitor line contains one JSON report. Byte counts are deltas since the preceding successfully published report,
saturating at 65535. Dropped-byte counts and buffer peaks are cumulative since boot; account for resets and saturation
when interpreting a test run. These are traffic statistics, not device messages or exact accounting if reports are lost.

`status` exits non-zero when no report arrives before its timeout. **Unlike the Python CLI, .NET `monitor` can finish
successfully with no reports.** Your runner must check sample availability and channel health, not just the exit code.
Without `--duration`, monitoring continues until interrupted. Device errors are printed to stderr and return non-zero.

## Other Commands And Controls

Read-only commands include `hardware`, `config`, `temperature`, and `version`. Hardware, configuration, and overrun
results are JSON; temperature and firmware version are plain values. Use `./PicoUart --help` for command options.

`toggle-led` changes the board LED. `reset` reboots the board and is refused unless firmware advertises reset support,
which is disabled by default. The dashboard and C# API can also save a persistent boot-time clock profile. Stop UART
traffic, close CDC ports, and read the [configuration precautions](../../docs/development/host-tools.md#persistent-configuration)
before changing it.

## Develop And Publish

Install the .NET 10 SDK to build from a checkout. From the repository root:

```sh
dotnet run --project host/dotnet/PicoUart -- status
dotnet run --project host/dotnet/PicoUart -- web --port 5001
```

On Linux/macOS/WSL, `tools/host/run-dotnet.sh` starts the dashboard; arguments such as `status` pass through unchanged.
The C# `PicoUartHid` class owns a connection and implements `IDisposable`; `Protocol` provides typed report decoders.
Python and .NET share `host/web`; keep frontend changes backend-neutral, not in a second C# frontend directory.

To publish a framework-dependent build (requires the .NET 10 ASP.NET Core Runtime on its target):

```sh
dotnet publish host/dotnet/PicoUart -c Release --self-contained false -o build/host-dotnet
dotnet build/host-dotnet/PicoUart.dll web --port 5001
```

For self-contained publishing, add `--self-contained true -r linux-x64` instead, or the appropriate target identifier.
Keep the whole output directory, including `web`. Use `-p:Version=1.2.3` to embed an explicit application version.
For release archives and packaging-only test runs, follow the [.NET release procedure](../../docs/deployment/releasing.md#net-packaging).

Unit and cross-backend integration commands are in the [.NET development guide](../../docs/development/host-tools.md#net-development).
Host tests do not establish physical UART behavior or qualify a release; see the [test index](../../docs/tests/README.md).

## Troubleshooting

- **Board not found:** check compatible firmware, the USB data cable, and any board selector. USB must be accessible to
	the computer running the host. Without a compatible board, HID commands failing is expected.
- **Access denied:** follow the Linux HID permission guide above. After using an older Python/libusb client, a detached
	HID driver may need the other client closed and the board's USB cable reconnected.
- **Missing web files:** re-extract the complete archive; do not move only the executable.
- **Need framework logs:** set `Logging__LogLevel__Microsoft=Information` before launching. Startup normally prints one
	dashboard URL while retaining warnings and errors.

See the [HID report reference](../../docs/design/usb/hid-report-reference.md) for compatibility and platform-specific
feature-report framing. The [security policy](../../SECURITY.md) covers the lab-only USB identity.