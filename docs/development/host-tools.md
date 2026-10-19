# Host Tools Development

This guide covers the Python and .NET host tools and their shared dashboard frontend. For installation and usage, see
the [Python package guide](../../host/python/README.md) and [.NET host guide](../../host/dotnet/README.md). The Python
package guide is also included as the PyPI project description.

The independent [WebHID prototype](../../host/webhid/README.md) runs directly in a supported desktop browser with its
own assets and renderer. Its hardware-free checks use Node rather than the Python/.NET test runners.

## Prerequisites

- Python 3.10 or newer for the Python host and cross-backend integration checks
- .NET 10 SDK for the C# host
- Desktop Chrome or Edge with WebHID support for the browser-only host
- Node 22 or newer for WebHID unit tests; no Node runtime is required to use a hosted page
- Bash on Linux/macOS, or PowerShell on Windows
- Network access to install hash-locked Python dependencies and restore .NET packages

## Python Environment Setup

To run an application from a checkout, use the [host launchers](../../tools/README.md); no separate setup command is
needed. For development and tests, explicitly create an environment and install the hash-locked development packages
from the repository root:

```sh
python3 -m venv host/python/.venv
host/python/.venv/bin/python -m pip install --require-hashes -r host/python/requirements-lock.txt
host/python/.venv/bin/python -m pip install --no-deps --no-build-isolation -e host/python
```

On native Windows, use `python` for the first command and `host/python/.venv/Scripts/python.exe` for subsequent commands.
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

Run package tests from `host/python`:

```bash
.venv/bin/python -m pytest -c pyproject.toml
```

For the full cross-project suite, Windows paths, and hardware-test boundaries, see the
[Test Documentation Index](../tests/README.md).

Measure package coverage from `host/python` with:

```bash
.venv/bin/python -m pytest --cov=pico_uart --cov-report=term-missing
```

## Python Dependencies and Locking

`requirements.txt` lists runtime dependencies. `requirements-dev.txt` adds the test runner. The Python launcher installs
`requirements-runtime-lock.txt` from public PyPI, without inheriting private pip index configuration. CI and release
qualification install `requirements-lock.txt` with hash verification. After changing direct dependencies, regenerate both
locks from
the repository root with:

```bash
uv pip compile host/python/requirements-dev.txt --universal --python-version 3.10 \
  --generate-hashes --no-emit-index-url --output-file host/python/requirements-lock.txt
uv pip compile host/python/requirements.txt --universal --python-version 3.10 \
  --generate-hashes --no-emit-index-url --output-file host/python/requirements-runtime-lock.txt
```

The [dependency contract tests](../../tests/contracts/test_firmware_contract.py) check direct pins, alignment between both
locks, and that the runtime lock contains no test packages. hidapi `0.14.0.post4` supplies a Python 3.13 Linux ARM64 wheel,
avoiding the original `0.14.0` source-build failure on that platform.

## Python Distribution

Build the wheel from the repository root:

```bash
uv build --wheel --directory host/python
```

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
See the [.NET host guide](../../host/dotnet/README.md#publish) for publishing and standalone deployment.

## WebHID Development

WebHID is a standalone browser app in `host/webhid`, independent of the Python/.NET server and shared
`host/web` frontend. Keep its assets and device code local to that app; user setup, deployment, architecture, and tests
are documented in the [WebHID guide](../../host/webhid/README.md).

WebHID accesses USB devices local to the browser; use the Python/.NET dashboard for remote boards. The
[HID Report Reference](../design/usb/hid-report-reference.md) owns the wire contract. Mocked Node tests do not prove
native browser/OS USB compatibility; see the [hardware test index](../tests/README.md) for physical validation.

## Python/.NET Web UI

Python and .NET serve the same frontend from `host/web`. Keep templates backend-neutral and preserve the shared JSON
contract; do not add a second frontend. The [.NET integration tests](../../tests/tooling/test_dotnet_host.py) guard
shared assets and API behavior. Distribution details belong in the [Python](../../host/python/README.md) and
[.NET](../../host/dotnet/README.md) guides.

Both dashboards separate device identity from live runtime status, retaining the six UART channel table.
The runtime cards include total RX overflow across all six channels alongside uptime, service load, RAM and temperature.
The
`/api/status` snapshot carries `identity` (product and serial) and `runtime` (uptime, per-core service-load basis points, reserved
and total RAM), with independent `identity_error` and `runtime_error` fields. Unsupported or failed feature reads clear
the affected values without discarding legacy channel/board reports; disconnect clears device identity and runtime.
Metadata is sampled every two seconds. See the [HID report reference](../design/usb/hid-report-reference.md) for
the authoritative field definitions, UID source, service-load semantics and legacy compatibility, and RAM accounting.

`led` carries nullable `manual_on` and `output_on` values, with an independent `led_error` on failed reads.
The Runtime status heading contains the physical LED indicator and manual toggle; sequence numbers remain internal
for report-gap detection rather than appearing in the UI.
