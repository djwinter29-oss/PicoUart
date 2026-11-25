# Host Tools Development

Use this guide to develop the Python package or .NET host from a checkout. For installation and everyday use, see the
[Python](../../host/python/README.md) and [.NET](../../host/dotnet/README.md) guides. For browser app development and
HTTP behavior, see the [dashboard behavior and presentation guide](dashboard-development.md).

Run commands from the repository root. Setup and tests here do not require a board. Most command examples use POSIX
shell syntax; the Python setup section includes the native PowerShell equivalent. Host applications use HID for board
status and controls; UART data and line coding stay on the six CDC ports. See the
[HID reference](../design/usb/hid-report-reference.md) for the USB contract.

## Python

### Python Environment Setup

Use Python 3.10+ and pip 25.1+. The editable install includes the `dev` dependency group:

```sh
python3 -m venv host/python/.venv
. host/python/.venv/bin/activate
python -m pip install --group host/python/pyproject.toml:dev
python -m pip install --no-build-isolation -e host/python
```

For native Windows, use the same steps with the Python executable inside the environment:

```powershell
python -m venv host/python/.venv
.\host\python\.venv\Scripts\Activate.ps1
python -m pip install --group host/python/pyproject.toml:dev
python -m pip install --no-build-isolation -e host/python
```

If PowerShell blocks activation, omit that line and use `.\host\python\.venv\Scripts\python.exe` instead of `python`
for commands run from the repository root. From `host/python`, use `.\.venv\Scripts\python.exe`.
Reinstall after changing dependencies. If pip has no hidapi wheel for your platform, it builds the package from source;
see the [hidapi installation instructions](https://github.com/trezor/cython-hidapi) for native prerequisites.
Python and .NET versions come from Git tags, so builds need full tag history.

### Tests and Checks

With the venv active, run the Python suite from the repository root:

```sh
python -m pytest -c pyproject.toml
```

Run Ruff and mypy from `host/python`; the POSIX command uses a subshell so the current directory stays at the
repository root:

```sh
(cd host/python && .venv/bin/python -m ruff check src tests && .venv/bin/python -m mypy)
```

For just the package tests, run `python -m pytest -c host/python/pyproject.toml host/python/tests` from the repository
root. Add `--cov=pico_uart --cov-report=term-missing` for coverage. Mypy enforces `disallow_untyped_defs` on the
package. Runtime dependencies are in `project.dependencies`; test and development tools are in the `test` and `dev`
groups in [pyproject.toml](../../host/python/pyproject.toml).

### Build a Wheel

Install [uv](https://docs.astral.sh/uv/getting-started/installation/) for this step. Stage the shared dashboard assets,
then build the wheel:

```sh
python -c "from shutil import copytree; copytree('host/web/src', 'host/python/src/pico_uart/dashboard/assets', dirs_exist_ok=True)"
uv build --wheel --directory host/python
```

The standard-library copy command works in POSIX shells and PowerShell. This builds locally; it does not publish.
`hatch-vcs` derives the version from Git tags, or uses `0.0.0.dev0` without Git metadata. Set
`SETUPTOOLS_SCM_PRETEND_VERSION` to override it. Publication instructions are in
[Host App Distribution and Publishing](../deployment/host-app-publishing.md).

## .NET Development

Use the .NET 10 SDK. `host/dotnet/PicoUart.sln` contains the app and xUnit tests.
The cross-backend pytest also needs the Python development environment above.

```sh
dotnet test host/dotnet/PicoUart.sln --filter "Category=Unit"
dotnet format host/dotnet/PicoUart.sln --verify-no-changes --no-restore
python -m pytest tests/tooling/test_dotnet_host.py
```

The unit tests use scripted HID streams and share [input-accounting cases](../../tests/contracts/host-input-cases.json)
with Python. The cross-backend test publishes the app and checks the shared frontend and HTTP contract. It is skipped
if `dotnet` is not on `PATH`; set `DOTNET_EXE` to use a specific executable.

### Run the Host

This starts the web host in the foreground; press `Ctrl+C` to stop it:

```sh
dotnet run --project host/dotnet/PicoUart -- web --port 5001
```

To publish a framework-dependent build locally:

```sh
dotnet publish host/dotnet/PicoUart -c Release --self-contained false -o build/host-dotnet
```

Run the published host in the foreground with `dotnet build/host-dotnet/PicoUart.dll web --port 5001`; press
`Ctrl+C` to stop it.

For a self-contained publish, add `--self-contained true -r linux-x64` (or another RID). Ship the whole output
directory, including `web/`. Use `-p:MinVerVersionOverride=1.2.3` to set the version.
