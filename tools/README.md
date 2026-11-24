# Repository Tools

The `tools/` directory contains local build, test, hardware, and release helpers.

## Categories

| Directory     | Purpose                                                                                          |
| ------------- | ------------------------------------------------------------------------------------------------ |
| `firmware/`   | Pico SDK setup, firmware builds, and Debug Probe loading                                         |
| `hil/`        | Reusable Python project for physical HIL runners and serial bridge/stress tools                  |
| `host/`       | Host application launchers and Linux HID permission helpers                                     |
| `release/`    | Release version, USB identity, and artifact verification                                         |
| `validation/` | Test runners, coverage, syntax checks, static analysis, and smoke tests |

## Common Commands

```sh
# Firmware
. tools/firmware/setup-sdk-env.sh
tools/firmware/build.sh --board pico                    # or --board pico2
python3 tools/firmware/config.py --build-dir build/firmware-pico --probe-serial <serial> read

# Host dashboards (Linux/macOS/WSL)
tools/host/run-python.sh                                # or run-dotnet.sh, run-direct.sh

# Host tests, C and Python (after the developer environment setup linked below)
PYTHON_EXE=host/python/.venv/bin/python tools/validation/run-host-tests.sh

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
instance needs a separate port and gets a port-specific session cookie. Run only one HID monitor per physical board;
select a different board for each process:

```sh
tools/host/run-python.sh --serial <serial-1> dashboard --port 5001
tools/host/run-dotnet.sh --serial <serial-2> web --port 5002
```

The same pattern works for multiple instances of either backend. The Direct Dashboard serves static files on loopback
port 8000; use `--port` to change it. WebHID permissions apply on the computer running the browser.

Run a launcher with `--help` for prerequisites and environment overrides. Python and .NET check local USB/HID
permissions before starting, but do not install rules automatically. The Direct Dashboard does not check permissions.

The Python launcher creates or reuses `host/python/.venv` (`VENV_DIR` overrides it) and installs the `dashboard` extra
from public PyPI if Flask or hidapi cannot be imported. It runs the source directly, without an editable install. After
dependency changes, refresh the environment using the
[developer setup](../docs/development/host-tools.md#python-environment-setup).
Native Windows users can use the direct commands in the [Python](../host/python/README.md),
[.NET](../host/dotnet/README.md), and [Direct Dashboard](../host/direct/README.md) guides.

## Linux HID Permissions

Run both commands from the account that will use the host application:

```sh
tools/host/hid-permissions-setup.sh
tools/host/hid-permissions-check.sh
```

Setup requires sudo, installs `/etc/udev/rules.d/70-picouart-hid.rules`, reloads udev, and reapplies rules to attached
devices. It grants read/write access to PicoUart USB and hidraw nodes (VID `0x2E8A`, PID `0x116B`) through the
invoking user's primary group and desktop-session `uaccess`. Customized rules are not overwritten. Separate USB and
hidraw rules support libusb-backed hidapi and hidraw/WebHID clients; CDC serial permissions and Debug Probe access
are unchanged.

HID permission includes board controls as well as diagnostics. See [security guidance](../SECURITY.md) and
[firmware build options](../firmware/build-and-config.md) for reboot behavior and how to disable standalone HID reboot.

If access is still denied:

- Reconnect the board and rerun the check. With no board attached, the check succeeds without testing connectivity.
- Systems without udev receive a warning and skip the check; both helpers are no-ops on non-Linux hosts.
- For the Direct Dashboard, run setup on the Linux computer running the browser.
