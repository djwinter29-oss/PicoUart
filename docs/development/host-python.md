# Host Python Development

This guide covers working on the Python package in [`host/python`](../../host/python/README.md).
The package's end-user installation and usage guide is its [README](../../host/python/README.md),
which is also included as the PyPI project description.

## Prerequisites

- Python 3.10 or newer
- Bash on Linux/macOS, or PowerShell on Windows
- Network access to install the hash-locked Python dependencies

## Environment Setup

From the repository root, the Bash helper creates `host/python/.venv`, installs
the locked development dependencies, and installs `pico-uart` in editable mode:

```bash
tools/host/setup-venv.sh
source host/python/.venv/bin/activate
```

On Windows PowerShell:

```powershell
.\tools\host\setup-venv.ps1
.\host\python\.venv\Scripts\Activate.ps1
```

Use `--venv-dir PATH` with Bash or `-VenvDir PATH` with PowerShell to choose a
different location. Relative paths are resolved from the current working
directory. See [tools/README.md](../../tools/README.md) for the repository's
host setup helper entry points.

## Layout

- `src/pico_uart/client.py`: public HID client API
- `src/pico_uart/transport.py`: device discovery and HID transport
- `src/pico_uart/protocol.py`: HID report constants and decoders
- `src/pico_uart/cli.py`: command-line interface
- `src/pico_uart/web/`: Flask dashboard and static assets
- `tests/`: hardware-free host tests

The CLI, dashboard, and tests should use the public `pico_uart` package rather
than adding another top-level compatibility module.

## Tests

Run the host Python suite from the repository root:

```bash
host/python/.venv/bin/python -m pytest -c host/python/pyproject.toml
```

On Windows, use `host/python/.venv/Scripts/python.exe` in place of the POSIX
interpreter path. The pytest suite does not require a board; hardware validation
and acceptance criteria are documented in the [test index](../tests/README.md).

The repository-wide host test runner is `tools/test/test-host.sh`.

## Dependencies and Locking

`requirements.txt` lists runtime dependencies. `requirements-dev.txt` adds the
test runner. CI and release qualification install `requirements-lock.txt` with
hash verification. After changing direct dependencies, regenerate that lock
from the repository root with:

```bash
uv pip compile host/python/requirements-dev.txt --universal --python-version 3.10 \
  --generate-hashes --no-emit-index-url --output-file host/python/requirements-lock.txt
```

The existing [dependency contract test](../../host/python/tests/test_firmware_contract.py)
checks that the direct pins appear in the generated lock.

## Build the Distribution

Build the wheel from the repository root:

```bash
uv build --wheel --directory host/python
```

The project metadata points PyPI at `host/python/README.md` for its long
description. Keep that file focused on package users; contributor setup and
maintenance instructions belong in this guide.