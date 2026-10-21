# PicoUart .NET Host

**Monitor six UARTs from your terminal, test runner, or browser.** The .NET host provides a diagnostics CLI, local
dashboard, and C# HID client for PicoUart on Raspberry Pi Pico and Pico 2.

Diagnostics use a separate USB HID interface, so your existing serial tools can keep using the six USB CDC ports.
This application does not capture UART payloads or set baud rates and framing; use your serial tools for those tasks.

## Requirements

- A computer matching one of the release targets below; no .NET or Python installation is needed
- A Raspberry Pi Pico or Pico 2 running compatible
	[PicoUart firmware](https://github.com/djwinter29-oss/PicoUart/releases), connected to this computer over USB
- Access to the board's HID interface; Linux may need
	[device permissions](../../tools/README.md#linux-hid-permissions)

## Install

Download a self-contained host archive from [GitHub Releases](https://github.com/djwinter29-oss/PicoUart/releases).
Choose the target for the computer running the application, not the Pico:

| Computer | Release Target | Archive |
| --- | --- | --- |
| Linux x64 | `linux-x64` | `.tar.gz` |
| Windows x64 | `win-x64` | `.zip` |
| Apple Silicon Mac | `osx-arm64` | `.tar.gz` |

Extract the **whole archive**, keeping its `web` directory and other files beside the executable. No .NET SDK, runtime,
or Python installation is needed. See
[distribution details](../../docs/deployment/host-app-distribution.md#net-downloads)
for supported downloads and packaging limitations.

## Quick Start

Connect the flashed board, then run from the extracted directory on Linux/macOS:

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
Use one HID monitor/server per physical board; separate processes do not share its input reports or traffic totals.

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
which is disabled by default. The dashboard can also save a persistent boot-time clock profile; a verified save
automatically reboots the board, independently of the optional reset command. Stop UART traffic, close CDC ports, and
read the [configuration precautions](../../docs/development/host-tools.md#persistent-configuration) before changing it.
After reconnecting, check the actual active profile and any fallback reason rather than assuming the requested clock
became active. A verified save does not establish overclock stability.

## Troubleshooting

- **Board not found:** check compatible firmware, the USB data cable, and any board selector. USB must be accessible to
	the computer running the host. Without a compatible board, HID commands failing is expected.
- **Access denied:** follow the Linux HID permission guide above. After using an older Python/libusb client, a detached
	HID driver may need the other client closed and the board's USB cable reconnected.
- **No valid reports:** check firmware compatibility and the USB connection. Missing reports are not zero traffic;
  verify that monitor output contains samples even when the command exits successfully.
- **Missing web files:** re-extract the complete archive; do not move only the executable.
- **Need framework logs:** set `Logging__LogLevel__Microsoft=Information` before launching. Startup normally prints one
	dashboard URL while retaining warnings and errors.

## Documentation

- [Project, firmware, and wiring](../../README.md)
- [Host development and source-checkout setup](../../docs/development/host-tools.md#net-development)
- [HID report reference and compatibility](../../docs/design/usb/hid-report-reference.md)
- [Security and lab-only USB identity](../../SECURITY.md)