# Repository Tools

The `tools/` directory contains local build, test, hardware, and release helpers.

## Categories

| Directory | Purpose |
| --- | --- |
| `firmware/` | Pico SDK setup, firmware builds, and Debug Probe loading |
| `hil/` | Physical HIL runners and serial bridge/stress tools |
| `host/` | Host application launchers and Linux HID permission helpers |
| `release/` | Release version, USB identity, and artifact verification |
| `validation/` | Test runners, coverage, syntax checks, static analysis, and smoke tests |

## Common Commands

```sh
# Firmware
. tools/firmware/setup-sdk-env.sh
tools/firmware/build.sh --board pico                    # or --board pico2
python3 tools/firmware/config.py --build-dir build/firmware-pico --probe-serial <serial> read

# Host dashboards (Linux/macOS/WSL)
tools/host/run-python.sh                                # or run-dotnet.sh, run-direct.sh

# Hardware tests
tools/hil/setup.sh
tools/hil/runner/full.sh --help

# Release artifact verification
python3 tools/release/verify-build.py --help
```

`config.py` also takes `set-clock` and `erase`; see [Firmware Build and Configuration](../firmware/build-and-config.md).
The other HIL runners and `pico-uart-hil` subcommands are covered in the
[HIL Fixture Test Plan](../docs/tests/hil-fixture-test-plan.md).

## Running Dashboards

The Python and .NET launchers default to the local dashboard and pass application arguments through unchanged. Each
instance needs a separate port and gets a port-specific session cookie:

```sh
tools/host/run-python.sh --serial <serial-1> dashboard --port 5001
tools/host/run-dotnet.sh --serial <serial-2> web --port 5002
```

The same pattern works for multiple instances of either backend. The Direct Dashboard serves static files on loopback
port 8000; use `--port` to change it. WebHID permissions apply on the computer running the browser.

Native Windows users can use the direct commands in the [Python](../host/python/README.md),
[.NET](../host/dotnet/README.md), and [Direct Dashboard](../host/direct/README.md) guides.

Run a launcher with `--help` for prerequisites and environment overrides. Python and .NET check local USB/HID
permissions before starting, but do not install rules automatically. The Direct Dashboard does not check permissions.

The Python launcher creates or reuses `host/python/.venv` (`VENV_DIR` overrides it) and installs the local `pico-uart`
package, resolving dependencies from public PyPI if Flask or hidapi cannot be imported. It runs the source directly.
After dependency changes, refresh the environment using the
[developer setup](../docs/development/host-tools.md#python-environment-setup).

## Linux HID Permissions

Run both commands from the account that will use the host application:

```sh
tools/host/hid-permissions-setup.sh
tools/host/hid-permissions-check.sh
```

For setup behavior and troubleshooting, see the
[Linux HID permissions guide](../docs/hardware-setup.md#linux-hid-permissions).
