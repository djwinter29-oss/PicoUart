# Host Tools Development

This guide covers the Python and .NET host tools and their shared dashboard frontend. For installation and usage, see
the [Python package guide](../../host/python/README.md) and [.NET host guide](../../host/dotnet/README.md). The Python
package guide is also included as the PyPI project description.

The independent [Direct Dashboard](../../host/direct/README.md) runs directly in a supported desktop browser with its
own assets and renderer. Its hardware-free checks use Node rather than the Python/.NET test runners.

Run commands from the repository root unless a linked guide explicitly says otherwise. These setup, build, and test
steps do not require a board; physical device checks require an identified board and separate hardware validation.

Python and .NET dashboards suppress routine HTTP request logs by default, including page visits and polling.
Warnings and errors remain visible.

## Prerequisites

- Python 3.10 or newer for the Python host and cross-backend integration checks
- .NET 10 SDK for the C# host
- Desktop Chrome or Edge with WebHID support for the browser-only host
- Node 22 or newer for WebHID unit tests; no Node runtime is required to use a hosted page
- pip 25.1 or newer for installing Python dependency groups
- Bash on Linux/macOS, or PowerShell on Windows
- Network access to install Python dependencies and restore .NET packages

Install only the tools needed for the application or checks you are changing. Source checkouts need Git and full tag
history for Git-derived Python versions; standalone source archives preserve their resolved version without Git.

## Python Environment Setup

To run an application from a checkout, use the [host launchers](../../tools/README.md); no separate setup command is
needed. For development and tests, explicitly create an environment and install the development dependency group and
editable package from the repository root:

```sh
python3 -m venv host/python/.venv
host/python/.venv/bin/python -m pip install --group host/python/pyproject.toml:dev
host/python/.venv/bin/python -m pip install --no-build-isolation -e host/python
```

On native Windows, use `python` to create the environment and replace `host/python/.venv/bin/python` with
`host/python/.venv/Scripts/python.exe` throughout this guide.
Repeat both install commands when dependency declarations change. The launchers are POSIX shell scripts for
Linux/macOS/WSL; native Windows application commands remain in the package guides.
If hidapi has no wheel for your Python/platform, installation needs its native build prerequisites. Installation errors
stop the Python launcher; it does not silently run without HID access. A supported Python version with an available
wheel avoids that source build. The WebHID launcher needs only the Python standard library.

On Linux, Python prefers the existing hidraw node even when hidapi uses libusb. This avoids detaching the kernel HID
driver and breaking subsequent .NET/WebHID discovery. A hidraw permission error is surfaced rather than retrying through
libusb. Boards left detached by an older client need a USB reconnect after that client closes.

## Python Layout

The installable package lives in `host/python/src/pico_uart`; use its public API rather than adding another top-level
compatibility module. Python and .NET share the frontend in `host/web/src`. Keep framework-specific behavior in each backend
and the shared assets backend-neutral.

## Python Tests

Run only the Python package tests:

```bash
host/python/.venv/bin/python -m pytest -c host/python/pyproject.toml host/python/tests
```

For repository Python checks, including tooling and cross-project contracts:

```bash
host/python/.venv/bin/python -m pytest -c pyproject.toml
```

Run the Python host lint and type checks from the package directory:

```bash
cd host/python
.venv/bin/python -m ruff check src tests
.venv/bin/python -m mypy
```

Mypy enforces `disallow_untyped_defs` across the Python package.

Measure package coverage with:

```bash
host/python/.venv/bin/python -m pytest -c host/python/pyproject.toml host/python/tests \
  --cov=pico_uart --cov-report=term-missing
```

These Python checks do not replace the separate .NET and Node suites or physical UART validation. See the
[test index](../tests/README.md) for validation levels and hardware boundaries.

## Python Dependencies

Edit dependency declarations only in [pyproject.toml](../../host/python/pyproject.toml):
`project.dependencies` contains the HID runtime package, `project.optional-dependencies.dashboard` contains Flask,
the `test` dependency group contains pytest, and `dev` adds the packages used by the full host test suite, build
checks, linting, and type checking. `build-system.requires` declares the isolated build backend and plugin.
Runtime, optional, and direct development requirements are version-pinned there; pip resolves transitive dependencies
during installation, so environments are not hash-locked. The runtime launcher uses public PyPI without inheriting
private index configuration.
The [dependency contract tests](../../tests/contracts/test_firmware_contract.py) check the declared packages and that no
generated lock files remain. Firmware artifact checks install only the `test` group.

## Python Client API

The [package quick start](../../host/python/README.md) covers installation and a first status read.
`PicoUartHid` owns its HID connection; use it as a context manager to close the connection on exit.
Select a board with `PicoUartHid(serial_number="ABC123")`. Device access failures may raise `OSError` or `RuntimeError`.
`close()` returns false if an in-flight feature transfer defers native-handle closure; call
`wait_closed(timeout_s=...)` to wait for that closure.
Run one HID monitor/server per board; see [shared monitoring](#board-selection-and-shared-monitoring).

### Board and channel reads

| Method | Returns |
| --- | --- |
| `read_status(timeout_ms=250)` | Latest six-channel Input snapshot, or `None` on timeout |
| `read_identity()` / `read_board_status()` | Static identity and capabilities; board status also includes temperature |
| `read_telemetry()` | Live board telemetry |
| `read_configuration()` | Cached boot-time pins, capacities, stored profile, and reset cause |
| `read_channel(index)` | An on-demand channel Feature snapshot, separate from Input traffic totals |
| `read_channel_diagnostic(index)` | RX fault history, last configuration-error reason, measurement age, and buffer peaks |
| `read_channel_diagnostics()` | Diagnostics for all six channels |

`read_status()` updates the channel whose Input report arrived; it does not wait for fresh data from all six channels.
Feature and diagnostic reads do not advance Input traffic baselines. Cached or unavailable UART measurements cannot
establish a clean current UART rate. Convenience methods include `read_runtime()`, `read_led()`,
`read_overflow_counts()`, and `read_hardware_info()`.
`timeout_ms=0` is implemented as a bounded 1 ms poll because hidapi interprets zero as an unbounded wait. When provided,
the absolute `deadline` also bounds client-lock acquisition and metadata reads.
`read_configuration(deadline=...)` likewise bounds client-lock acquisition, including when returning cached
configuration; if the deadline expires before that return, it raises `TimeoutError`. Feature-read errors retain the
usual HID `OSError`/`RuntimeError` behavior.

The supported high-level API is `PicoUartHid` and the methods described here, plus the module-level functions
`read_board_status`, `read_board_temperature`, `read_firmware_version`, `read_configuration`, `read_hardware_info`,
`read_overflow_counts`, `read_telemetry`, and `reset_board`.
Module-level helpers accept an already-open HID device and share in-flight transfer and deferred-close state with
other wrappers around that same object. After a timed-out helper call, later helpers fail until its native transfer
returns; calling `close()` on any wrapper closes the shared handle. Use `PicoUartHid` as a context manager for
multi-operation sessions.
The names in `pico_uart.__all__` are the supported package-root exports. Report parsers, IDs and sizes, USB
identifiers, raw HID access, and generic feature-report helpers are low-level exports tied to the firmware report
contract. Prefer their owning `pico_uart.hid.protocol`, `pico_uart.hid.transport`, and `pico_uart.hid.client` modules
when using them directly.
The optional third-party hidapi module is available as `pico_uart.hid.transport.hid`.

### Python module-path migration

The top-level `PicoUartHid` API remains unchanged, but implementation-module imports moved:

| Previous path | Current path |
| --- | --- |
| `pico_uart.cli` | `pico_uart.main` |
| `pico_uart.client` | `pico_uart.hid.client` |
| `pico_uart.protocol` | `pico_uart.hid.protocol` |
| `pico_uart.transport` | `pico_uart.hid.transport` |
| `pico_uart.web` | `pico_uart.dashboard` |

Use `pico-uart` or `python -m pico_uart.main`; `python -m pico_uart` is no longer supported. This is an intentional
breaking change for callers that import these implementation modules directly. The old raw hidapi root export is
also replaced by `pico_uart.hid.transport.hid`.

### UART readiness

After opening and configuring all selected CDC TTYs and asserting DTR, use
`board.wait_channels_ready({0: 115200, 2: 115200}, timeout_s=3.0)` before transmitting. This waits for open, ready
channels with applied 8N1 settings within 3% of the requested baud and no pending control. It raises `OSError` for
current initialization/configuration failures, `TimeoutError` on expiry, and propagates HID access errors. Historical
fault counters do not fail readiness. Select the HID board by the same USB serial number as the TTY endpoints;
this helper does not open or configure TTYs and does not change generic POSIX `open()` behavior.

### Board controls

The CLI's `toggle-led` and `reset` commands correspond to `toggle_led()` and `reset_board()`.
Reset requires firmware-advertised support, enabled by default.
`save_configuration(profile, unsafe_overclock=False)` writes a supported boot-time clock profile;
`read_configuration_save()` reads the save result. A verified save automatically reboots the board, independently of
the optional reset command. Follow the [save safety contract](../design/usb/hid-report-reference.md#save-and-reboot-safety)
and [persistent configuration workflow](#persistent-configuration) before changing settings.
Actions use one serialized controlling host. Timeout or disconnect means the outcome is unconfirmed; never
automatically retry reboot or save. There is no request ID or multi-client ownership guarantee.
An LED toggle is successful once firmware confirms the command; a later telemetry refresh failure does not turn that
confirmed action into a failure.

The .NET dashboard bounds waiting for its control queue to five seconds. Control cancellation tokens are honored
while acquiring the HID client and between read-only preflight steps, including immediately before native SET begins.
A Feature read already in progress is not canceled by the token; its existing deadline and deferred-cleanup rules still
apply. After SET begins, result verification and uncertainty handling continue even if the request is canceled.

## Python Test Automation

Run diagnostics alongside the serial test; these commands do not start a test or capture UART payloads.
Record dropped-byte counts around a timed capture, running the serial test in another terminal during monitoring:

```sh
pico-uart overruns > overruns-before.txt
pico-uart monitor --duration 60 > uart-traffic.log
pico-uart overruns > overruns-after.txt
```

`pico-uart status --json > uart-status.json` saves a status snapshot. Python `status` is the only Python command
with `--json`. .NET `status` always prints the same JSON object; its `--json` flag is accepted and does not
change the output. Python `monitor` prints text. .NET `monitor` prints one channel object per line and can
finish with no reports.

The status object is `{"channels": [ ... ]}` with six entries. A channel that has not delivered an Input in
this process keeps the waiting placeholder (`sequence` null, `state` `"unknown"`) plus traffic-accounting
fields (`traffic`, `totals`, `traffic_gaps`, `traffic_incomplete`, `observed_interval_ms`, `updated_at`).
The Input that arrived is that channel's first sample: `traffic` values stay null, `totals` are the boot
counters, `observed_interval_ms` stays 0, and `traffic_incomplete` is true when `interval_ms` is 0 or 65535.
`updated_at` is UTC with millisecond precision and a `+00:00` offset. Status object keys are sorted on both hosts.

Each channel has independently sequenced 500 ms measurement intervals. Status JSON tracks observed totals,
valid observed intervals, sequence gaps, and sticky incompleteness indicators. Missing reports or saturated values
prevent exact traffic accounting. Dropped-byte counts and buffer peaks are cumulative since boot, not per-test totals;
compare before and after a run, accounting for resets and saturation.

`status` on either host retries until its timeout. The default is 2 seconds, and each read waits at most 250 ms.
Python and .NET select that limit with `--timeout`. `status` fails when the timeout expires with no valid report.
Python `monitor` fails if it receives no valid reports, but can succeed after one report even if later reads time out.
Python and .NET `monitor` pass the remaining capture deadline through metadata and Input reads and exclude reports
that complete at or after the deadline. Both hosts fail on device errors.
A successful command does not establish continuous reporting or healthy channels; the runner must evaluate reported
states and counters rather than treating the exit code as the test result.

Dashboard save and board-action errors after command submission report that the outcome is unconfirmed, including
when the connection drops before a result is read. Reconnect and inspect board state before deciding what to do; never
automatically retry a save or reboot. The hosted dashboard bounds complete HTTP requests, including response bodies;
a timed-out write is still unconfirmed because the server may have completed it. Firmware rejections with a confirmed
result are reported separately. Both Python and .NET retain an unconfirmed save in the shared board snapshot across
reconnects, and both use the same wording so the browser can gate a save after a page reload. After
a fresh status read, explicitly inspect and acknowledge the stored profile before retrying.

## Python Distribution

The publication channels and artifact qualification gates are in the
[host app distribution guide](../deployment/host-app-distribution.md).
The commands below build locally; they do not publish to PyPI.

Stage the shared dashboard into the Python package, then build the wheel from the repository root:

```bash
mkdir -p host/python/src/pico_uart/dashboard/assets
cp -R host/web/src/. host/python/src/pico_uart/dashboard/assets/
uv build --wheel --directory host/python
```

Hatchling uses `hatch-vcs` to derive version `1.2.3` from Git tag `v1.2.3` and development versions from later commits.
The nested package uses the repository root and full Git tag history to derive development and release versions.
Source distributions preserve the resolved version without Git. With neither Git metadata nor a source-distribution
version, package metadata falls back to `0.0.0.dev0`, which the dashboard displays as `0.0.0-debug`. Use
`SETUPTOOLS_SCM_PRETEND_VERSION` for an explicit test-build version.

The release workflows stage the canonical `host/web/src` dashboard into the Python package before building wheels.
Hatchling's artifact rule includes those staged files in wheels and source distributions, including files that are
untracked in a checkout. Package tests require the repository checkout and are not included in distributions.
Tag-push releases use Git versions; manual release preflights override the version with their input tag, and manual
PyPI test publications override it with a marked development version.

The project metadata points PyPI at `host/python/README.md` for its long description. Keep that file focused on package
users; contributor setup and maintenance instructions belong in this guide.

## .NET Development

The solution in `host/dotnet/PicoUart.sln` contains the host application and its xUnit test project. From the repository
root:

```sh
dotnet run --project host/dotnet/PicoUart -- status
dotnet run --project host/dotnet/PicoUart -- web --port 5001
```

`status` is a read-only device check and can fail normally with no compatible board attached; it is not a setup test.
Its JSON matches the Python `channels` snapshot in [Python test automation](#python-test-automation). .NET
`monitor` still prints one channel object per line.
On Linux/macOS/WSL, `tools/host/run-dotnet.sh` starts the dashboard; arguments such as `status` pass through unchanged.
The C# `PicoUartHid` class owns a connection and implements `IDisposable` and `IAsyncDisposable`;
`Protocol` provides typed report decoders.
The HID implementation separates responsibilities without changing that public client:

- [PicoUartHid.cs](../../host/dotnet/PicoUart/Hid/PicoUartHid.cs) owns the connection, caches, and typed reads.
- [PicoUartHid.Actions.cs](../../host/dotnet/PicoUart/Hid/PicoUartHid.Actions.cs) implements serialized board controls
  as part of the same client, sharing its lock and timeout quarantine rather than opening another connection.
- [HidDiscovery.cs](../../host/dotnet/PicoUart/Hid/HidDiscovery.cs) handles enumeration, selection, and USB identity.
- [HidReports.cs](../../host/dotnet/PicoUart/Hid/HidReports.cs) adapts descriptors and native Feature framing;
  [Protocol.cs](../../host/dotnet/PicoUart/Wire/Protocol.cs) remains responsible for product payload validation.
- [HidStreamLifetime.cs](../../host/dotnet/PicoUart/Hid/HidStreamLifetime.cs) owns in-flight native transfers and
  deferred cleanup, including the per-device barrier against reopening before cleanup completes.

Dashboard source names reflect their ownership:

- [BoardMonitor.cs](../../host/dotnet/PicoUart/Dashboard/BoardMonitor.cs) owns one board's polling, controls, and reconnects.
- [DashboardBoardRegistry.cs](../../host/dotnet/PicoUart/Dashboard/DashboardBoardRegistry.cs) discovers boards and shares
  their monitors between viewers.
- [DashboardSnapshot.cs](../../host/dotnet/PicoUart/Dashboard/DashboardSnapshot.cs) folds samples into API state and
  traffic accounting.

Python and .NET share `host/web/src`; tests live in `host/web/tests`. Keep frontend changes backend-neutral rather than
adding a separate C# frontend.

Build and run the hardware-free unit suite:

```sh
dotnet build host/dotnet/PicoUart.sln
dotnet test host/dotnet/PicoUart.sln --filter "Category=Unit"
dotnet format host/dotnet/PicoUart.sln --verify-no-changes --no-restore
```

Unit tests use scripted HID streams without opening physical devices. Shared
[Input-accounting cases](../../tests/contracts/host-input-cases.json) exercise populated Python/.NET snapshots, including
non-nominal intervals, wraparound, sequence gaps, and cached UART measurements. Cross-backend integration checks publish
the .NET application and verify its shared frontend and HTTP contract:

```sh
host/python/.venv/bin/python -m pytest tests/tooling/test_dotnet_host.py
```

These integration checks skip when `dotnet` is not on `PATH`; set `DOTNET_EXE` to another SDK executable if needed.
The shared `/api/status` contract includes per-channel lifecycle, Input sequence/interval, nullable applied framing,
and `overflow_counts` derived from explicitly fetched diagnostics (unknown entries are null). Board-action results
are returned by action endpoints, not as a .NET-only `control` field in the status snapshot.

Host tests validate software contracts, not physical USB/HID enumeration or UART signaling. For board testing, use the
[test documentation index](../tests/README.md); release qualification remains separate and requires the exact packaged
artifacts and matching hashes described in the [release HIL gates](../deployment/releasing.md#release-hil-gates).

### Local .NET Publishing

Publish a framework-dependent build when the target has the .NET 10 ASP.NET Core Runtime installed:

```sh
dotnet publish host/dotnet/PicoUart -c Release --self-contained false -o build/host-dotnet
dotnet build/host-dotnet/PicoUart.dll web --port 5001
```

For a self-contained build, replace `--self-contained false` with `--self-contained true -r linux-x64`, or the matching
target identifier. Distribute the whole output directory, including `web`; the executable alone is insufficient.
Use `-p:MinVerVersionOverride=1.2.3` to embed an explicit application version. Release archive qualification and
marked packaging tests belong in the [publishing runbook](../deployment/host-app-publishing.md#net-packaging); user
installation belongs in the [.NET host guide](../../host/dotnet/README.md#install).

<a id="webhid-development"></a>

## Direct Dashboard Development

The Direct Dashboard uses WebHID and is a standalone browser app in `host/direct/src`, independent of the Python/.NET
server and shared `host/web/src` frontend. Tests live in `host/direct/tests`. Its HTML, CSS, favicon, and renderer are owned
by the source directory; no shared asset-generation step
or files from `host/web/src` are required. Browser connection and troubleshooting belong in the
[Direct Dashboard user guide](../../host/direct/README.md).

<a id="run-webhid-from-a-checkout"></a>

### Run Direct Dashboard From A Checkout

Serve the static app from the repository root with its version derived from Git tags:

```sh
tools/host/run-direct.sh
```

Open <http://127.0.0.1:8000/>. Use `--port N` to choose another port. The launcher stages a temporary copy and leaves
the canonical source tree unchanged. Without a POSIX shell, run the staging helper directly:

```sh
python tools/host/direct_site.py serve --port 8000
```

The server only delivers files; native HID access is still to the browser computer's USB devices. Do not open the HTML
directly from disk. Self-hosted deployments need HTTPS or localhost. Direct versions use the nearest reachable `vMAJOR.MINOR.PATCH` tag, with development versions incrementing the patch
and including the commit distance and hash. The Pages workflow stamps the validated release tag into a staged copy.
Release gates and deployment/rollback steps belong in the
[Pages publishing runbook](../deployment/host-app-publishing.md#direct-dashboard-pages).

### WebHID Checks

Run the hardware-free unit checks:

```sh
node --check host/direct/src/js/view.mjs
node --check host/direct/src/js/app.mjs
node --test host/direct/tests/*.test.mjs
```

The Direct Dashboard CI job also installs pinned Playwright and Chromium, then runs the simulated-device browser
smoke flow:

```sh
python -m pip install playwright==1.63.0
python -m playwright install --with-deps chromium
python host/direct/tests/browser_smoke.py
```

The test suite also checks the independent and shared renderers' presentation contracts; it does not open native USB
devices or qualify browser/OS device access.

Keep browser-specific feature-report framing and version validation covered by these tests. The HID reference owns
board-derived Serial / UID, service-load, and RAM-reservation semantics; do not replace them with browser-session
estimates.

WebHID accesses USB devices local to the browser; use the Python/.NET dashboard for remote boards. The
[HID Report Reference](../design/usb/hid-report-reference.md) owns the wire contract. Mocked Node tests do not prove
native browser/OS USB compatibility; see the [hardware test index](../tests/README.md) for physical validation.

## Python/.NET Web UI

The backend label and **App v** version share a secondary line beneath **PicoUart** in the top bar. This identifies the
host application, not the board's firmware. Python exposes its installed version through `pico_uart.__version__`;
.NET uses its assembly informational version. The backends pass these values into the shared HTML template;
`host/web/src` contains no application version metadata. Both derive versions from `vMAJOR.MINOR.PATCH` Git tags:
Python uses `hatch-vcs`, while .NET uses MinVer with patch auto-increment and the `dev` prerelease label.
`PICO_UART_HOST_VERSION` is only an optional runtime display override; an unset or empty value uses native build
metadata. Python's no-Git fallback is `0.0.0-debug`. Reinstall an editable Python package after commits or tags change
its version. None of these settings changes the board's firmware version.

Python and .NET serve the same frontend from `host/web/src`. Keep templates backend-neutral and preserve the shared JSON
contract; do not add a second frontend. The [.NET integration tests](../../tests/tooling/test_dotnet_host.py) guard
shared assets and API behavior. Distribution details belong in the [Python](../../host/python/README.md) and
[.NET](../../host/dotnet/README.md) guides.

The shared page loads native ES modules; no bundler or generated frontend copy is needed during development:

- [dashboard.js](../../host/web/src/js/dashboard.js): page state, board selection, status polling, and control workflows.
- [api.mjs](../../host/web/src/js/api.mjs): bounded JSON requests and request timeouts.
- [traffic.mjs](../../host/web/src/js/traffic.mjs): rate windows, freshness, and counter history.
- [view.mjs](../../host/web/src/js/view.mjs): display formatting and reusable channel rows.
- [diagnostics.mjs](../../host/web/src/js/diagnostics.mjs): on-demand dialog reads, cached results, and read age.

Keep module imports explicit. Diagnostics receive current board/selection state through a callback so late responses
remain isolated from a newly selected board. When changing a module's cache version, update all importers to the same
URL; the HTML entry also has a cache version. Both hosts package the entire asset tree, including module dependencies.
The Python host explicitly serves JavaScript modules and CSS with browser-compatible MIME types, independent of OS
file associations, so `nosniff` does not prevent them from loading.
Run the shared behavior and presentation checks with:

```sh
node --test host/web/tests/*.test.mjs host/direct/tests/*.test.mjs
```

All dashboards hide the header's sample age while reports are fresh. When updates exceed the existing freshness
threshold, they show `Telemetry stale` with the sample age; a connected board without a sample shows
`Waiting for telemetry`. Disconnected boards hide the sample-age indicator.

### Board selection and shared monitoring

The **Board** dropdown lists boards attached to the dashboard server, not the browser's computer. The page scans this
list only at startup or when the user clicks **Rescan**, without changing an existing selection. Status refresh uses
the backend's `refresh_interval_ms` and does not rescan the board list, even when disconnected or after a failed scan.
Python and .NET derive this hint from the latest accepted channel Input's valid `interval_ms`, clamped to 500–1000 ms.
The initial hint is 500 ms; zero/saturated intervals and duplicate/old reports do not change it. The hosted dashboard
uses the same bounds and returns to 500 ms on board selection, disconnection, or HTTP failure.
Initially the page selects the first discovered board (or the only
board). With `--serial` or `--device-path`, that board is the preferred initial selection; all boards remain available,
and an absent preferred board leaves the page unselected.

Selection is local to each page: different users or tabs can view different boards on the same server. Selecting a
board clears the previous board's display and rate history immediately. LED and reset commands target only that page's
selected board. Unplugging never silently selects another board; rescans retain a missing selection as **not found**.
Boards with serial numbers reconnect by serial even if the OS path changes. Devices without serials are identified by
their OS path, which cannot distinguish a replacement device reusing that path.

Each server starts one HID reader lazily per selected board and shares its cached snapshot among all viewers.
Additional viewers do not read the HID stream or consume its deltas again. Readers remain active when no page is viewing
them, retry disconnected boards, and are reaped after repeated scans no longer find their board. Observed traffic totals
belong to the shared reader's current connection, not to a browser session. Run only one host monitor/server per
physical board; separate server processes and standalone HID clients do not share these readers.

Closing a registry requests monitor shutdown and waits for active readers within the host's shutdown deadline. If that
deadline expires, reader cleanup remains owned and continues in the background. HID clients defer closing their native
handle until in-flight transfers return; a replacement reader never opens before cleanup completes. While cleanup is
pending, requests for that board receive a retryable error instead of blocking an HTTP worker. Results from an old HID
session are discarded if the monitor has already connected a replacement session. A rediscovered board becomes
available to status requests as soon as its retired reader has stopped; another board-list rescan is not required.

`GET /api/boards` returns `boards` (`id`, `product_name`, `serial_number`, `device_path`) and `default_board_id`.
The .NET host also returns `discovery_errors` (`device_path`, optional `serial_number`, and `error`) for individual
devices it could not inspect; a failed device does not hide other successfully discovered boards. The shared dashboard
shows these errors after a rescan.
Pass the opaque `id` as `?board=ID` to `GET /api/status` and `POST /api/actions/{toggle-led,reset}`. A status request
without a board returns an empty six-channel snapshot; actions require a known board and the existing session CSRF
token. Unknown IDs and duplicate board identities fail explicitly. Servers remain loopback-only; this is not a change
to network exposure or access control. WebHID retains its native browser chooser and is unchanged.

Session cookies are backend- and port-specific: `PicoUart.Python.Session.<port>` and `PicoUart.DotNet.Session.<port>`.
This prevents incompatible cookies from colliding when switching hosts on the same address and port. Older shared-name
cookies are ignored; reload the dashboard after upgrading to obtain a new session and CSRF token.

### Status presentation

All dashboards use Device overview, Hardware & boot, Live status, and UART channels sections. The boot profile is
editable when available; buffer capacities and pin mapping are read-only. Section naming does not change control
availability or protocol fields.
`/api/status` carries `identity` (product, serial, firmware, MCU/build target, capability/profile masks, reserved/total RAM)
and `runtime` (uptime, `service_load_basis_points`, and current clock),
with independent `identity_error` and `runtime_error` fields. Unsupported or failed reads clear
the affected values without discarding unrelated valid reports; disconnect clears device identity and runtime.
Board telemetry, including uptime, is read at the same bounded cadence as one HID report; static device information is
read once per connection, and combined channel snapshots arrive every 500 ms (see
[host polling](../design/usb/hid-report-reference.md#host-polling-and-validation)). The
[HID report reference](../design/usb/hid-report-reference.md) defines the version-1 design, including UID source,
service-load semantics, and RAM accounting.

Snapshots can be partially available. Static identity, firmware version, MCU, capabilities, and total RAM come from
report 1; temperature, LED state, uptime, service load, and current clock come from report 3.
Overflow counts come from each channel's `rx_overrun_count`, not board telemetry; .NET reports null when UART
measurements are Unavailable, rather than treating placeholder zeros as measured counts. A telemetry failure
clears affected board values without discarding valid channel snapshots, static information,
boot configuration, or command results. A device-information failure must not substitute invented identity or
capabilities, and board controls remain disabled until capabilities have been validated.
Clients must inspect the relevant error fields rather than treating missing metadata as zero or the whole board as
disconnected.

`configuration` reports the stored profile request, channel pins/count, reset cause, and four buffer capacities
using read-only HID Feature report 2; `configuration_error` is independent. Cache it on connection and
clear it when disconnected; saving does not change this boot snapshot. No credentials or raw
flash contents are returned.

`led` carries nullable `manual_on` and `output_on` values, with an independent `led_error` on failed reads.
The **Board LED: On/Off** action sends an explicit On or Off command through report 20, then rereads report 3;
its indicator reports physical output. The API's `toggle-led` action selects the opposite of a valid observed state
rather than sending a wire-level toggle. Unknown output is shown as `--` and disables the action.
Sequence numbers remain internal for gap detection
rather than appearing in the UI.
The connection indicator includes report age, updated on each status refresh and marked stale after three seconds without a
changed status sample. Age measures browser receipt time so remote host clock offsets do not distort freshness.

### Persistent configuration

All three dashboards expose the stored boot profile under **Hardware & boot**. The system-clock selector
offers the connected MCU's supported profiles and identifies its SDK default. A connected board advertising save support
allows profile selection even without ready UART channels or a stored profile; an unset stored profile means SDK default.
Controls may be temporarily locked during an in-flight action or save/reboot. Save retains its safety checks and requires
valid configuration data. Editing the selection never writes the
device, and telemetry refreshes do not overwrite unsaved edits. A non-default profile requires explicit unsafe-overclock
acknowledgement before **Save & Reboot**.

**Save & Reboot** persists the choice through
[HID report 20](../design/usb/hid-report-reference.md#report-id-20-board-action--result), waits for a matching action's
verified result, and displays failures explicitly. Close all CDC ports and stop external UART
traffic first; flash programming pauses servicing on both cores. A successful save starts the firmware's automatic
reboot grace period; the reboot does not wait for browser acknowledgement. The dashboard warns that USB will disconnect
briefly. After reconnecting, inspect the stored request and live system clock rather than assuming the
requested profile became active. A verified save does not establish overclock stability.
An unavailable save capability or failed control-report read disables Save without hiding other valid diagnostics.
Follow the [save safety contract](../design/usb/hid-report-reference.md#save-and-reboot-safety).

Python and .NET expose `POST /api/configuration?board=<id>` with the same session CSRF header as board actions.
The JSON object must contain exactly `profile` (integer `0` through `4`, using the IDs in the linked HID report) and
`unsafe_overclock` (boolean); request bodies are limited to 1 KiB on both hosts.
The selected board's client checks MCU compatibility and acknowledgement; firmware validates them independently.
HTTP success means the serialized save action reported Completed after verification, not just a successful HID transfer.
Python returns `{"ok": true}`; .NET returns the terminal action result. The result has no request ID or echoed profile;
only one controlling host may write.
Timeout/disconnect leaves an unconfirmed outcome and must not cause an automatic destructive retry.
Python and .NET retain an unconfirmed save warning in the shared board snapshot across polling and reconnects. The
device exposes one retained, uncorrelated control result, so reading a terminal result cannot prove it belongs to the
uncertain request and does not clear the warning. Only a later save confirmed successful by that request clears it.
Browser acknowledgement is local to the current page; a reload shows the warning again until a save is confirmed.
Snapshots expose the independent `configuration_save` result (`status`, `error_reason`) and
`configuration_save_error` plus the machine-readable `configuration_save_unconfirmed` flag; the existing
`configuration` remains read-only boot metadata. Control error responses include `outcome_unconfirmed` so the shared
renderer does not infer retry safety from localized or changeable error text.

### Channel diagnostics and traffic presentation

All three dashboards show applied baud/framing from the
[channel snapshots](../design/usb/hid-report-reference.md#reports-10-15-channel-snapshots) and GPIOs from report 2,
never from a hard-coded board map or a CDC request that might have been rejected.
`GET /api/diagnostics?board=<id>` explicitly refreshes `channel_diagnostics` (six records in channel order).
`diagnostics_error` is independent; failed reads surface an error and do not replace old measurements with zeros,
while unrelated valid reports remain visible. The .NET refresh is all-or-nothing: it publishes a new array only after
all six reads succeed. Any failed read returns a non-success HTTP response with `error`, including when earlier
channels succeeded; native I/O/access failures return 503 and timeouts return 504. The previous successful array and
derived overflow counts remain last-known data, with `diagnostics_error` set until a complete refresh succeeds.
The browser does not advance diagnostic read age on a failed refresh.
Versioned HID payloads require their current version-1 layouts; no
legacy decoder or missing-report compatibility mode is provided. IDs 10-15 each publish a complete 30-byte channel
snapshot every 500 ms and support the same layout through read-only Feature access. Feature reads can obtain
initial or on-demand state, but their overlapping traffic deltas never enter rates or totals.
Reports 30-35 independently provide two RX history counters, last configuration-error reason, acquisition age,
four byte-accurate peaks, and one shared latest-fault uptime. Read these on demand, not at routine Input cadence.

Channel API records carry independent `sequence`, `updated_at`, `traffic_gaps`, `traffic_incomplete`, and
`observed_interval_ms` alongside byte `totals`. The interval accumulates complete observed Input intervals,
excluding the first sample; renderers compute rate windows from its change rather than host receipt timing.
Feature reads supply state only and never advance these traffic-observation fields.
`traffic` contains per-Input modulo-32-bit byte increments, not rates, with null for an unusable interval or UART
measurement. Earlier .NET builds scaled these fields into bytes/second; consumers should use the shared byte semantics.
The renderer derives rates from totals and observed intervals rather than depending on that historical discrepancy.

Lifecycle badges distinguish Closed, Starting, Ready, Configuring, Error, and host-side Unknown/stale.
Historical RX faults and the last configuration error remain separate from current lifecycle; they do not turn a
recovered channel into an active failure. UART measurement validity is independent of lifecycle: a stopped backend
can provide a Current sample. Diagnostics are read-only and cannot be cleared or acknowledged.

The four directions display observed KB/s (1 KB = 1000 bytes) over windows of at least one second. Hosts derive
increments by modulo-subtracting successive 32-bit firmware counters, then divide summed increments by summed device
measurement intervals, not host arrival timing. The counters show bytes since MCU boot modulo $2^{32}$ and survive
dashboard reconnects. Reports 10-15 arrive nominally every 500 ms; track 16-bit sequence, interval, and freshness
separately per channel.
Use each channel's first Input sample to seed its displayed boot counter and establish the rate baseline. A sequence
gap resets that channel's current rate window, but the next counter recovers totals unless more than one wrap could
have occurred between samples. Initial/stale windows and windows containing report gaps or zero/saturated intervals
show `--` for the affected rate; they recover after a clean window. Three seconds without a new Input sample makes
that channel's rates stale. After staleness or a host read pause, treat rate continuity as uncertain even when a
16-bit sequence matches. One channel's gap does not invalidate other channels. `traffic_gaps` counts sequence-loss,
stale/rebaseline, and ignored out-of-order events. An invalid interval marks continuity incomplete but is not itself a
sequence gap; an invalid first interval therefore does not increment `traffic_gaps`. The monotonic count lets each
page invalidate its current rate window while retaining the sticky `traffic_incomplete` notice.
When a .NET channel becomes stale, `state` becomes `unknown`; `channel_state`, `cdc_open`, UART measurement validity,
and applied framing become null rather than claiming current Ready/open state. Counters, sequence, and the last Input
timestamp remain historical. Initial Feature retries cannot restore live fields on a stale channel; the next accepted
Input restores state and establishes a new rate baseline.

The independent WebHID renderer and hosted renderer retain their separate assets; the shared Node presentation tests
exercise the same rate, read-only fault flag, fallback and state contracts against both.
