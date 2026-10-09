# PicoUart .NET Host

The .NET 10 host provides a C# HID client, diagnostics CLI, and an ASP.NET Core dashboard. It uses the exact same
HTML, CSS, and JavaScript as the Python dashboard, sourced from `host/web`. UART data and line settings still belong
to the six CDC interfaces; this application only uses the diagnostics HID interface.

The shared frontend layout is:

```text
host/web/
	index.html
	favicon.svg
	css/dashboard.css
	js/dashboard.js
```

## Run

Install the .NET 10 SDK, then run from the repository root:

```sh
dotnet run --project host/dotnet/PicoUart -- status
dotnet run --project host/dotnet/PicoUart -- hardware
dotnet run --project host/dotnet/PicoUart -- monitor --duration 10
dotnet run --project host/dotnet/PicoUart -- web --port 5001
```

The dashboard binds only to `http://127.0.0.1:5001` in this example. Both servers implement `/api/status` and
`/api/actions/{toggle-led,reset}` with the same JSON field names. The HTML's `{{ csrf_token }}` placeholder is filled
by each server, and control requests require that session's token in `X-CSRF-Token`. Keep shared frontend changes
backend-neutral; do not add Flask-only template expressions or a second frontend under the C# project.

Other commands are `overruns`, `hardware`, `temperature`, `version`, `toggle-led`, and `reset`. Status, monitor samples,
hardware info, and overflow counts are emitted as JSON; `--json` is also accepted. Temperature and version are plain
values. Select a board with
`--serial SERIAL` or `--device-path PATH`, but not both. The client rejects ambiguous matches. Linux HID access may
require suitable hidraw permissions. Reset remains capability-gated and sends the arm command before the reset command.

The public `PicoUartHid` class owns the connection and implements `IDisposable`. `Protocol` exposes the pure report
decoders and typed results. HidSharp supplies cross-platform USB HID discovery and transport.

`ReadHardwareInfo()` and the `hardware` command query feature report 6 for MCU identity and the SDK-reported current
system clock in Hz. Results retain the raw `mcu_id`; unrecognized values display as `Unknown MCU (ID)` without losing
their clock. The shared dashboard shows the model and clock in MHz. Older firmware leaves these fields unknown
and reports a hardware-information error without discarding existing board metadata or channel telemetry.

HidSharp 2.6.4 returns an additional report-ID prefix for numbered feature reads on Linux. The client accounts for it
explicitly and validates both IDs; Windows/macOS use a single prefix. Feature buffers also accommodate Windows' maximum
feature-report length. Keep the framing checks when changing the HID dependency.

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

The xUnit project uses VSTest and reports each case independently. It tests report sizes, signatures, versions,
endianness, temperature boundaries, health bits, platform feature framing, dashboard traffic and sequence accounting,
JSON field names, disconnected controls, and CLI validation. All unit tests are hardware-free and tagged `Category=Unit`.
To run only protocol tests, add `--filter "FullyQualifiedName~ProtocolTests"` instead of the category filter. Add
`--logger trx --results-directory build/host-dotnet-tests` to retain a test report.

The Python integration checks publish the app and verify shared asset bytes, JSON shape, host restrictions, CSRF, and
CLI validation. They skip when `dotnet` is not on `PATH`; set `DOTNET_EXE` to use another SDK executable. CI runs both
suites on Linux and Windows.

Physical device discovery, telemetry, LED, reset, unplug/reconnect, and platform-specific HID behavior still require
board validation. Do not treat these host checks as HIL qualification; use the [test index](../../docs/tests/README.md).