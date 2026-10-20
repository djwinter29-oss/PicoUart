# PicoUart .NET Host

The .NET 10 host provides a C# HID client, diagnostics CLI, and an ASP.NET Core dashboard. It uses the exact same
HTML, CSS, and JavaScript as the Python dashboard, sourced from `host/web`. UART data and line settings still belong
to the six CDC interfaces; this application only uses the diagnostics HID interface.

## Run

Install the .NET 10 SDK, then run from the repository root:

```sh
tools/host/run-dotnet.sh                 # Dashboard (Linux/macOS/WSL)
tools/host/run-dotnet.sh status          # CLI
```

The launcher defaults to `web` on port 5000. Application arguments pass through unchanged. Native Windows users,
or anyone preferring the SDK directly, can use:

```sh
dotnet run --project host/dotnet/PicoUart -- status
dotnet run --project host/dotnet/PicoUart -- hardware
dotnet run --project host/dotnet/PicoUart -- config
dotnet run --project host/dotnet/PicoUart -- monitor --duration 10
dotnet run --project host/dotnet/PicoUart -- web --port 5001
```

Web startup prints one dashboard URL; framework warnings and errors remain visible. For framework diagnostic logs, set
`Logging__LogLevel__Microsoft=Information` before launching.

If the page loads but reports no HID interface, check the error in the page or `/api/status`, then follow
[Linux HID permissions](../../tools/README.md#linux-hid-permissions). After using older Python builds backed by libusb,
Linux's HID driver may remain detached: close the other client and reconnect the board's USB data cable to restore it.

The dashboard binds only to `http://127.0.0.1:5001` in this example. Use **Board** and **Rescan** to select among attached
boards independently in each page; viewers of the same board share one reader.
The [shared dashboard guide](../../docs/development/host-tools.md#board-selection-and-shared-monitoring) owns selection,
reader lifetime, CLI defaults, and the board-scoped API contract.
Both servers use the same JSON field names. The HTML's `{{ csrf_token }}` placeholder is filled
by each server, and control requests require that session's token in `X-CSRF-Token`. Keep shared frontend changes
backend-neutral; do not add Flask-only template expressions or a second frontend under the C# project.

Other commands are `overruns`, `hardware`, `config`, `temperature`, `version`, `toggle-led`, and `reset`. Status, monitor samples,
hardware info, and overflow counts are emitted as JSON; `--json` is also accepted. Temperature and version are plain
values. Select a board with
`--serial SERIAL` or `--device-path PATH`, but not both. The client rejects ambiguous matches. Linux HID access may
require [HID permissions setup](../../tools/README.md#linux-hid-permissions).
Reset remains capability-gated and sends the arm command before the reset command.

The public `PicoUartHid` class owns the connection and implements `IDisposable`. `Protocol` exposes the pure report
decoders and typed results. HidSharp supplies cross-platform USB HID discovery and transport.

`ReadHardwareInfo()` and the `hardware` command query feature report 6 for MCU identity and system clock. See the
[HID Report Reference](../../docs/design/usb/hid-report-reference.md) for payload and compatibility details.
`ReadConfiguration()` and the `config` command read the persistent configuration source, fallback reason, and selected
clock profiles from HID report 16. The report is read-only and does not expose credentials or raw flash contents.
`ReadConfigurationSave()` reads report 17; `SaveConfiguration(profile, unsafeOverclock)` saves a next-boot profile
and polls for completion with a bounded timeout, rejecting an already pending save before writing.
Profile 0 keeps the SDK default (no clock change); profiles 1 and 2
require explicit unsafe overclock acknowledgement and the matching RP2040 or RP2350 family. All saves require a known
MCU family. Stop UART traffic and close CDC ports before saving.
Saving does not reboot: apply the saved setting by manually rebooting the board.

The dashboard's board-scoped `POST /api/configuration?board=...` accepts exactly
`{"profile":0,"unsafe_overclock":false}` with the session's `X-CSRF-Token`, and returns `{"ok":true}` only after the
save completes. `/api/status` exposes `configuration_save` and optional `configuration_save_error` independently of
the immutable boot decision in `configuration`.

Numbered HID feature reports have platform-specific framing. Preserve the client's framing validation and tests when
changing the HID transport dependency.

## Publish

```sh
dotnet publish host/dotnet/PicoUart -c Release -o build/host-dotnet
dotnet build/host-dotnet/PicoUart.dll web --port 5001
```

The published directory includes the shared frontend, so it runs without the source checkout or Python. For a machine
without .NET installed, publish for its runtime identifier with `--self-contained true -r linux-x64` (or the appropriate
Windows/macOS identifier). Distribute the whole output directory, including its `web` folder.

## Checks

```sh
dotnet test host/dotnet/PicoUart.sln --filter "Category=Unit"
host/python/.venv/bin/python -m pytest tests/tooling/test_dotnet_host.py
```

The xUnit unit suite is hardware-free and tagged `Category=Unit`. To run only protocol tests, use
`--filter "FullyQualifiedName~ProtocolTests"`; add `--logger trx --results-directory build/host-dotnet-tests` to retain
a test report.

The Python integration checks publish the app and validate the shared frontend/API contract. They skip when `dotnet` is
not on `PATH`; set `DOTNET_EXE` to use another SDK executable. CI runs both suites on Linux and Windows.

Physical device discovery, telemetry, LED, reset, unplug/reconnect, and platform-specific HID behavior still require
board validation. Do not treat these host checks as HIL qualification; use the [test index](../../docs/tests/README.md).