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
dotnet build host/dotnet/PicoUart.sln
dotnet test host/dotnet/PicoUart.sln --filter "Category=Unit"
```

Unit tests run without a board. Cross-backend integration checks publish the .NET application and verify its shared
frontend and HTTP contract:

```sh
host/python/.venv/bin/python -m pytest tests/tooling/test_dotnet_host.py
```

These integration checks skip when `dotnet` is not on `PATH`; set `DOTNET_EXE` to another SDK executable if needed.
See the [.NET host guide](../../host/dotnet/README.md#develop-and-publish) for publishing and standalone deployment.

## WebHID Development

WebHID is a standalone browser app in `host/webhid`, independent of the Python/.NET server and shared
`host/web` frontend. Keep its assets and device code local to that app; user setup, deployment, architecture, and tests
are documented in the [WebHID guide](../../host/webhid/README.md).

Run the hardware-free browser checks used by CI:

```sh
node --check host/webhid/js/view.mjs
node --check host/webhid/js/app.mjs
node --test host/webhid/tests/*.test.mjs
```

The test suite also checks the independent and shared renderers' presentation contracts; it does not open native USB
devices or qualify browser/OS device access.

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
list only at startup or when the user clicks **Rescan**, without changing an existing selection. Status refreshes once
per second and does not rescan the board list, even when disconnected or after a failed scan.
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
`/api/status` carries `identity` (product and serial) and `runtime` (uptime, per-core service-load basis points,
reserved and total RAM), with independent `identity_error` and `runtime_error` fields. Unsupported or failed reads clear
the affected values without discarding unrelated valid reports; disconnect clears device identity and runtime.
Metadata, including uptime, is sampled every second. See the
[HID report reference](../design/usb/hid-report-reference.md) for the authoritative version-1 field definitions, UID
source, service-load semantics, and RAM accounting.

Snapshots can be partially available. `metadata_error` covers the grouped board-status and overflow-count reads; a
failure clears `board` and `hardware` and marks each overflow count unknown, while independent identity/runtime data
can remain valid.
If only the later hardware-info read fails, `hardware_error` is set without discarding board status or overflow counts.
Clients must inspect the relevant error fields rather than treating missing metadata as zero or the whole board as
disconnected.

`configuration` reports the source (defaults, stored, or recovery), fallback reason, stored profile, and active profile
using read-only HID feature report 16; `configuration_error` is independent. The snapshot and both dashboards refresh
this diagnostic with other metadata once per second and clear it when unavailable or disconnected. No credentials or raw
flash contents are returned.

`led` carries nullable `manual_on` and `output_on` values, with an independent `led_error` on failed reads.
The **Board LED: On/Off** action toggles the manual LED setting; its output indicator reports physical state, which USB
activity may also affect. Unknown output is shown as `--`. Sequence numbers remain internal for report-gap detection
rather than appearing in the UI.
The connection indicator includes report age, updated every second and marked stale after three seconds without a
changed status sample. Age measures browser receipt time so remote host clock offsets do not distort freshness.

### Persistent configuration

All three dashboards expose **Persistent configuration** separately from live telemetry. The system-clock selector
offers the connected MCU's supported profiles and identifies its SDK default. Editing the selection never writes the
device, and telemetry refreshes do not overwrite unsaved edits. A non-default profile requires explicit unsafe-overclock
acknowledgement before **Save & Reboot**.

**Save & Reboot** persists the choice through
[HID report 17](../design/usb/hid-report-reference.md#report-17-save-persistent-clock-profile), waits for a matching
verified result, and displays failures explicitly. Close all CDC ports and stop external UART
traffic first; flash programming pauses servicing on both cores. A successful save starts the firmware's automatic
reboot grace period; the reboot does not wait for browser acknowledgement. The dashboard warns that USB will disconnect
briefly. After reconnecting, inspect the actual boot-selected profile and any fallback reason rather than assuming the
requested profile became active. A verified save does not establish overclock stability.
Unavailable save reports disable Save without hiding other valid diagnostics.

Python and .NET expose `POST /api/configuration?board=<id>` with the same session CSRF header as board actions.
The JSON object must contain exactly `profile` (integer `0`, `1`, or `2`) and `unsafe_overclock` (boolean).
The selected board's client checks MCU compatibility and acknowledgement; firmware validates them independently.
HTTP success (`{"ok": true}`) means firmware reported a verified save, not just a successful HID transfer.
Snapshots expose the independent `configuration_save` result (`status`, nullable `profile`) and
`configuration_save_error`; the existing `configuration` remains read-only boot metadata.

### Channel diagnostics and traffic presentation

All three dashboards show applied baud/framing and TX/RX GPIOs from the
[channel diagnostic reports](../design/usb/hid-report-reference.md#reports-10-15-channel-diagnostics), never from a
hard-coded board map or a CDC request that might have been rejected. Snapshots expose `channel_diagnostics` (six records
in channel order) and an independent `diagnostics_error`; failed reads clear those records and surface an error,
while unrelated valid reports remain visible. Versioned HID payloads require their current version-1 layouts; no
legacy decoder or missing-report compatibility mode is provided. Diagnostics refresh with metadata every second in
all three dashboards.

Each channel shows a health badge and simple error flags with short troubleshooting tooltips, without expandable
history, counts, or detection times. The underlying diagnostic records remain available to host tools.
Asserted init/control failures are labelled **Current** when telemetry is fresh; RX latches and past fault observations
are labelled **Recorded**. Stale flags are labelled **Last reported**, not current. These labels do not infer physical
disconnection or an RX signal level.
Channel diagnostics are read-only: dashboards cannot clear or acknowledge faults, and recorded faults keep the channel
in **Attention** while telemetry is fresh, even if the corresponding live flag is no longer asserted.
Healthy, closed CDC channels without observed throughput are labelled **Idle**, not disconnected: UART RX remains
active and reported faults remain visible even with CDC closed.

The four directions display observed B/s over windows of at least one second. Hover a rate for cumulative observed
bytes since the monitor connected. Initial/stale windows and windows containing report gaps or saturated deltas show
`--`; they recover after a clean window. Three seconds without a new status sample makes rates stale. `traffic_gaps`
is a monotonic count of incomplete status observations, allowing each page to invalidate its current rate window while
retaining the sticky `traffic_incomplete` notice. Totals remain lower bounds after missing/saturated reports.

The independent WebHID renderer and hosted renderer retain their separate assets; the shared Node presentation tests
exercise the same rate, read-only fault flag, fallback and state contracts against both.
