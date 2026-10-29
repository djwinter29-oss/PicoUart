# Host Tools Development

This guide covers the Python and .NET host tools and their shared dashboard frontend. For installation and usage, see
the [Python package guide](../../host/python/README.md) and [.NET host guide](../../host/dotnet/README.md). The Python
package guide is also included as the PyPI project description.

The independent [WebHID dashboard](../../host/webhid/README.md) runs directly in a supported desktop browser with its
own assets and renderer. Its hardware-free checks use Node rather than the Python/.NET test runners.

Run commands from the repository root unless a linked guide explicitly says otherwise. These setup, build, and test
steps do not require a board; physical device checks require an identified board and separate hardware validation.

## Prerequisites

- Python 3.10 or newer for the Python host and cross-backend integration checks
- .NET 10 SDK for the C# host
- Desktop Chrome or Edge with WebHID support for the browser-only host
- Node 22 or newer for WebHID unit tests; no Node runtime is required to use a hosted page
- uv for the Python build and lock-generation commands below; ordinary package installation uses pip or pipx
- Bash on Linux/macOS, or PowerShell on Windows
- Network access to install hash-locked Python dependencies and restore .NET packages

Install only the tools needed for the application or checks you are changing. Source checkouts need Git and full tag
history for Git-derived Python versions; standalone source archives preserve their resolved version without Git.

## Python Environment Setup

To run an application from a checkout, use the [host launchers](../../tools/README.md); no separate setup command is
needed. For development and tests, explicitly create an environment and install the hash-locked development packages
from the repository root:

```sh
python3 -m venv host/python/.venv
host/python/.venv/bin/python -m pip install --require-hashes -r host/python/requirements-lock.txt
host/python/.venv/bin/python -m pip install --no-deps --no-build-isolation -e host/python
```

On native Windows, use `python` to create the environment and replace `host/python/.venv/bin/python` with
`host/python/.venv/Scripts/python.exe` throughout this guide.
Repeat both install commands when dependency manifests or locks change. The launchers are POSIX shell scripts for
Linux/macOS/WSL; native Windows application commands remain in the package guides.
If hidapi has no wheel for your Python/platform, installation needs its native build prerequisites. Installation errors
stop the Python launcher; it does not silently run without HID access. A supported Python version with an available
wheel avoids that source build. The WebHID launcher needs only the Python standard library.

On Linux, Python prefers the existing hidraw node even when hidapi uses libusb. This avoids detaching the kernel HID
driver and breaking subsequent .NET/WebHID discovery. A hidraw permission error is surfaced rather than retrying through
libusb. Boards left detached by an older client need a USB reconnect after that client closes.

## Python Layout

The installable package lives in `host/python/src/pico_uart`; use its public API rather than adding another top-level
compatibility module. Python and .NET share the frontend in `host/web`. Keep framework-specific behavior in each backend
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

Measure package coverage with:

```bash
host/python/.venv/bin/python -m pytest -c host/python/pyproject.toml host/python/tests \
  --cov=pico_uart --cov-report=term-missing
```

These Python checks do not replace the separate .NET and Node suites or physical UART validation. See the
[test index](../tests/README.md) for validation levels and hardware boundaries.

## Python Dependencies and Locking

Edit dependency declarations only in [pyproject.toml](../../host/python/pyproject.toml): `project.dependencies` contains
runtime packages, the `test` dependency group contains pytest, and `dev` adds coverage and build tools.
`build-system.requires` declares the isolated build backend and plugin. TOML contains version requirements, not hashes.

The lock files are generated installation inputs; do not edit their versions or hashes by hand:

| Generated File | Used For |
| --- | --- |
| [requirements-runtime-lock.txt](../../host/python/requirements-runtime-lock.txt) | Runtime-only launcher and installed-release checks |
| [requirements-lock.txt](../../host/python/requirements-lock.txt) | Development and host CI, including test and build tools |
| [requirements-voltage-lock.txt](../../host/python/requirements-voltage-lock.txt) | Minimal Linux/Python 3.12 pytest environment for firmware-artifact checks |

These describe dependency environments, not PicoUart release versions. Installers retain `--require-hashes`; the Python
launcher uses public PyPI without inheriting private index configuration. Regenerate all three locks from the repository
root in this order, constraining the smaller environments to the development lock's versions:

```bash
uv pip compile host/python/pyproject.toml --group host/python/pyproject.toml:dev \
  --universal --python-version 3.10 \
  --generate-hashes --no-emit-index-url --output-file host/python/requirements-lock.txt
uv pip compile host/python/pyproject.toml --constraint host/python/requirements-lock.txt \
  --universal --python-version 3.10 \
  --generate-hashes --no-emit-index-url --output-file host/python/requirements-runtime-lock.txt
uv pip compile --group host/python/pyproject.toml:test --constraint host/python/requirements-lock.txt \
  --python-version 3.12 --python-platform x86_64-unknown-linux-gnu \
  --generate-hashes --no-emit-index-url --output-file host/python/requirements-voltage-lock.txt
```

The [dependency contract tests](../../tests/contracts/test_firmware_contract.py) check direct pins, runtime/development
alignment, and that the runtime lock contains no test packages. The
[firmware CI contracts](../../tests/contracts/test_voltage_ci_contract.py) verify the minimal lock's exact pytest
subset.
hidapi `0.14.0.post4` supplies a Python 3.13 Linux ARM64 wheel,
avoiding the original `0.14.0` source-build failure on that platform.

## Python Client API

The [package quick start](../../host/python/README.md) covers installation and a first status read.
`PicoUartHid` owns its HID connection; use it as a context manager to close the connection on exit.
Select a board with `PicoUartHid(serial_number="ABC123")`. Device access failures may raise `OSError` or `RuntimeError`.
Run one HID monitor/server per board; see [shared monitoring](#board-selection-and-shared-monitoring).

### Board and channel reads

| Method | Returns |
| --- | --- |
| `read_status(timeout_ms=1000)` | Latest six-channel Input snapshot, or `None` on timeout |
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

The supported high-level API is `PicoUartHid` and the methods described here, plus the module-level functions
`read_board_status`, `read_board_temperature`, `read_firmware_version`, `read_configuration`, `read_hardware_info`,
`read_overflow_counts`, `read_telemetry`, and `reset_board`.
Report parsers, IDs and sizes, USB identifiers, raw HID access, and generic feature-report helpers are low-level
exports tied to the firmware report contract. Prefer their owning `pico_uart.protocol`, `pico_uart.transport`,
and `pico_uart.client` modules when using them directly.

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

## Python Test Automation

Run diagnostics alongside the serial test; these commands do not start a test or capture UART payloads.
Record dropped-byte counts around a timed capture, running the serial test in another terminal during monitoring:

```sh
pico-uart overruns > overruns-before.txt
pico-uart monitor --duration 60 > uart-traffic.log
pico-uart overruns > overruns-after.txt
```

`pico-uart status --json > uart-status.json` saves a status snapshot. Only `status` supports `--json`;
`monitor` produces text with channel lifecycle, measurement validity, and UART/CDC byte deltas.
Each channel has independently sequenced 500 ms measurement intervals. Status JSON tracks observed totals,
valid observed intervals, sequence gaps, and sticky incompleteness indicators. Missing reports or saturated values
prevent exact traffic accounting. Dropped-byte counts and buffer peaks are cumulative since boot, not per-test totals;
compare before and after a run, accounting for resets and saturation.

`status` fails if its timeout expires without a valid report. `monitor` fails if it receives no valid reports,
but can succeed after one report even if later reads time out. Both fail on device errors.
A successful command does not establish continuous reporting or healthy channels; the runner must evaluate reported
states and counters rather than treating the exit code as the test result.

## Python Distribution

The publication channels and artifact qualification gates are in the
[host app distribution guide](../deployment/host-app-distribution.md).
The commands below build locally; they do not publish to PyPI.

Build the wheel from the repository root:

```bash
uv build --wheel --directory host/python
```

Hatchling uses `hatch-vcs` to derive version `1.2.3` from Git tag `v1.2.3` and development versions from later commits.
The nested package searches parent directories for the repository; fetch full history and tags for release builds.
Source distributions preserve the resolved version without Git. With neither Git metadata nor a source-distribution
version, the fallback is `0.0.0.dev0`. Use `SETUPTOOLS_SCM_PRETEND_VERSION` for an explicit test-build version.

`hatch_build.py` bundles the canonical `host/web` assets so source distributions build without the rest of the
repository.
Package tests require the repository checkout and are not included in source distributions or wheels.
Hatchling, its VCS plugin, and the editable-install helper are included in the locked development environment.
Builds validate the selected dashboard asset tree and fail on missing required files instead of emitting an incomplete
host package.
Keep `build-system.requires` pins aligned with the TOML `dev` group and regenerate the development lock when
updating them. Tag-push releases use Git versions; manual release preflights override the version with their input tag,
and manual PyPI test publications override it with a marked development version.

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
On Linux/macOS/WSL, `tools/host/run-dotnet.sh` starts the dashboard; arguments such as `status` pass through unchanged.
The C# `PicoUartHid` class owns a connection and implements `IDisposable`; `Protocol` provides typed report decoders.
Python and .NET share `host/web`; keep frontend changes backend-neutral rather than adding a separate C# frontend.

Build and run the hardware-free unit suite:

```sh
dotnet build host/dotnet/PicoUart.sln
dotnet test host/dotnet/PicoUart.sln --filter "Category=Unit"
```

Unit tests run without a board. Cross-backend integration checks publish the .NET application and verify its shared
frontend and HTTP contract:

```sh
host/python/.venv/bin/python -m pytest tests/tooling/test_dotnet_host.py
```

These integration checks skip when `dotnet` is not on `PATH`; set `DOTNET_EXE` to another SDK executable if needed.
The shared `/api/status` contract includes per-channel lifecycle, Input sequence/interval, nullable applied framing,
and `overflow_counts` derived from explicitly fetched diagnostics (unknown entries are null). Board-action results
are returned by action endpoints, not as a .NET-only `control` field in the status snapshot.

### Local .NET Publishing

Publish a framework-dependent build when the target has the .NET 10 ASP.NET Core Runtime installed:

```sh
dotnet publish host/dotnet/PicoUart -c Release --self-contained false -o build/host-dotnet
dotnet build/host-dotnet/PicoUart.dll web --port 5001
```

For a self-contained build, replace `--self-contained false` with `--self-contained true -r linux-x64`, or the matching
target identifier. Distribute the whole output directory, including `web`; the executable alone is insufficient.
Use `-p:Version=1.2.3` to embed an explicit application version. Release archive qualification and marked packaging
tests belong in the [publishing runbook](../deployment/host-app-publishing.md#net-packaging); user installation belongs
in the [.NET host guide](../../host/dotnet/README.md#install).

## WebHID Development

WebHID is a standalone browser app in `host/webhid`, independent of the Python/.NET server and shared
`host/web` frontend. Its HTML, CSS, favicon, and renderer are owned by that directory; no shared asset-generation step
or files from `host/web` are required. Browser connection and troubleshooting belong in the
[WebHID user guide](../../host/webhid/README.md).

### Run WebHID From A Checkout

Serve the static app from the repository root:

```sh
tools/host/run-webhid.sh
```

Open <http://127.0.0.1:8000/>. Use `--port N` to choose another port. Without a POSIX shell, serve the same directory
with Python 3:

```sh
python -m http.server 8000 --bind 127.0.0.1 --directory host/webhid
```

The server only delivers files; native HID access is still to the browser computer's USB devices. Do not open the HTML
directly from disk. Self-hosted deployments need HTTPS or localhost. Local files display the debug app version; the
Pages workflow stamps it during packaging. Release gates and deployment/rollback steps belong in the
[Pages publishing runbook](../deployment/host-app-publishing.md#webhid-pages).

### WebHID Checks

Run the hardware-free browser checks used by CI:

```sh
node --check host/webhid/js/view.mjs
node --check host/webhid/js/app.mjs
node --test host/webhid/tests/*.test.mjs
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
host application, not the board's firmware. Python obtains its installed version through `importlib.metadata`; .NET
uses its assembly informational version. The backends pass these values into the shared HTML template; `host/web`
contains no application version metadata. Tagged Python builds derive their package version from Git through
`hatch-vcs`; .NET builds use `dotnet publish -p:Version=...`.
`PICO_UART_HOST_VERSION` is only an optional runtime display override; an unset or empty value uses native build
metadata.
Python Git checkouts produce VCS-derived versions; the no-Git fallback `0.0.0.dev0` displays as `0.0.0-debug`.
.NET uses `0.0.0-debug` for development. Reinstall an editable Python package after commits or tags change its version.
None of these settings changes the board's firmware version.

Python and .NET serve the same frontend from `host/web`. Keep templates backend-neutral and preserve the shared JSON
contract; do not add a second frontend. The [.NET integration tests](../../tests/tooling/test_dotnet_host.py) guard
shared assets and API behavior. Distribution details belong in the [Python](../../host/python/README.md) and
[.NET](../../host/dotnet/README.md) guides.

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
Additional viewers do not read the HID stream or consume its deltas again. Readers remain active until server shutdown,
including when no page is viewing them, and retry disconnected boards. Observed traffic totals belong to the shared
reader's current connection, not to a browser session. Run only one host monitor/server per physical board; separate
server processes and standalone HID clients do not share these readers.

`GET /api/boards` returns `boards` (`id`, `product_name`, `serial_number`, `device_path`) and `default_board_id`.
Pass the opaque `id` as `?board=ID` to `GET /api/status` and `POST /api/actions/{toggle-led,reset}`. A status request
without a board returns an empty six-channel snapshot; actions require a known board and the existing session CSRF
token. Unknown IDs and duplicate board identities fail explicitly. Servers remain loopback-only; this is not a change
to network exposure or access control. WebHID retains its native browser chooser and is unchanged.

### Status presentation

Both dashboards separate device identity from live runtime status, retaining the six UART channel table.
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
Overflow counts come from each channel's `rx_overrun_count`, not board telemetry. A telemetry failure
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

All three dashboards expose **Persistent configuration** separately from live telemetry. The system-clock selector
offers the connected MCU's supported profiles and identifies its SDK default. Editing the selection never writes the
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
`unsafe_overclock` (boolean).
The selected board's client checks MCU compatibility and acknowledgement; firmware validates them independently.
HTTP success (`{"ok": true}`) means the serialized save action reported Completed after verification,
not just a successful HID transfer. The result has no request ID or echoed profile; only one controlling host may write.
Timeout/disconnect leaves an unconfirmed outcome and must not cause an automatic destructive retry.
Snapshots expose the independent `configuration_save` result (`action`, `status`, `error_reason`) and
`configuration_save_error`; the existing `configuration` remains read-only boot metadata.

### Channel diagnostics and traffic presentation

All three dashboards show applied baud/framing from the
[channel snapshots](../design/usb/hid-report-reference.md#reports-10-15-channel-snapshots) and GPIOs from report 2,
never from a hard-coded board map or a CDC request that might have been rejected.
`GET /api/diagnostics?board=<id>` explicitly refreshes `channel_diagnostics` (six records in channel order).
`diagnostics_error` is independent; failed reads surface an error and do not replace old measurements with zeros,
while unrelated valid reports remain visible. Versioned HID payloads require their current version-1 layouts; no
legacy decoder or missing-report compatibility mode is provided. IDs 10-15 each publish a complete 30-byte channel
snapshot every 500 ms and support the same layout through read-only Feature access. Feature reads can obtain
initial or on-demand state, but their overlapping traffic deltas never enter rates or totals.
Reports 30-35 independently provide two RX history counters, last configuration-error reason, acquisition age,
four byte-accurate peaks, and one shared latest-fault uptime. Read these on demand, not at routine Input cadence.

Channel API records carry independent `sequence`, `updated_at`, `traffic_gaps`, `traffic_incomplete`, and
`observed_interval_ms` alongside byte `totals`. The interval accumulates complete observed Input intervals,
excluding the first sample; renderers compute rate windows from its change rather than host receipt timing.
Feature reads supply state only and never advance these traffic-observation fields.

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
16-bit sequence matches. One channel's gap does not invalidate other channels. `traffic_gaps` is a monotonic count of
incomplete status observations, allowing each page to invalidate its current rate window while retaining the sticky
`traffic_incomplete` notice.

The independent WebHID renderer and hosted renderer retain their separate assets; the shared Node presentation tests
exercise the same rate, read-only fault flag, fallback and state contracts against both.
