# Repository Tools

The `tools/` directory contains local build, test, hardware, and release helpers. Use the root [README](../README.md) as
the project documentation index; this file only maps the tool categories.

## Categories

| Directory     | Purpose                                                                                          |
| ------------- | ------------------------------------------------------------------------------------------------ |
| `firmware/`   | Pico SDK setup, firmware builds, and Debug Probe loading                                         |
| `hil/`        | Reusable Python project for physical HIL runners and serial bridge/stress tools                  |
| `host/`       | Host application launchers and Linux HID permission helpers                                     |
| `release/`    | Release version, USB identity, and artifact verification                                         |
| `validation/` | Test runners, lock filtering, coverage helpers, syntax checks, static analysis, and smoke checks |

## Common Commands

```sh
# Firmware
. tools/firmware/setup-sdk-env.sh --sdk-version 2.3.0
tools/firmware/build.sh --board pico
tools/firmware/build.sh --board pico2

# Host dashboards (Linux/macOS/WSL; choose one)
tools/host/run-python.sh
tools/host/run-dotnet.sh
tools/host/run-webhid.sh

# Host tests (after the developer environment setup linked below)
PYTHON_EXE=host/python/.venv/bin/python tools/validation/run-host-tests.sh
tools/validation/validate.sh --skip-build

# Hardware test project
tools/hil/setup.sh
tools/hil/.venv/bin/python -m pytest tools/hil/tests
tools/hil/runner/full.sh --help

# Release artifact verification
python3 tools/release/verify-build.py --help
```

Python and .NET launchers default to `web` and pass application arguments through unchanged, for example
`tools/host/run-python.sh --serial ABC123 status` or `tools/host/run-dotnet.sh web --port 5001`.
Both backend dashboards default to port 5000; select a different port to run them together.
The WebHID launcher serves only static assets on loopback port 8000; use `--port` to change it.
Run each launcher with `--help` for prerequisites and environment overrides.
The Python/.NET launchers check local USB/HID permissions before starting; they do not install rules automatically.
WebHID does not check the server's USB permissions because the browser may run on another computer.

The Python launcher creates or reuses `host/python/.venv` (`VENV_DIR` overrides it), installing the hash-locked
runtime dependencies only if Flask or hidapi cannot import. Bootstrap uses public PyPI only and ignores machine/user
pip index configuration, avoiding unrelated private feeds. It runs source directly, without activating the environment or
installing the package editable. Refresh dependencies explicitly after dependency changes using the
[developer setup](../docs/development/host-tools.md#python-environment-setup).
Native Windows users can use the direct commands in the [Python](../host/python/README.md),
[.NET](../host/dotnet/README.md), and [WebHID](../host/webhid/README.md) guides.

## Linux HID Permissions

Run once from the account that will use the host application:

```sh
tools/host/hid-permissions-setup.sh
tools/host/hid-permissions-check.sh
```

Setup requests sudo, installs `/etc/udev/rules.d/70-picouart-hid.rules`, reloads udev, and reapplies rules to attached
devices. The rules grant read/write access only to PicoUart `cafe:4010` USB and hidraw nodes through the invoking user's
primary group, plus desktop-session `uaccess`. Group membership is not changed; SSH users need no fresh login.
The USB rule supports libusb-backed hidapi; the hidraw rule supports HID clients and WebHID. CDC serial permissions and
Debug Probe access are unchanged. Customized existing rules are never overwritten.

If access is still denied, reconnect the board and rerun the check. With no connected board the check succeeds; this is
not a connectivity test. On systems without udev it warns and skips checking. Both helpers are no-ops on non-Linux hosts.
For WebHID, run setup on the Linux computer running the browser, not just on a remote dashboard server.

## Canonical Documentation

- [Project README](../README.md)
- [Firmware build and configuration](../firmware/build-and-config.md)
- [HIL Fixture Test Plan](../docs/tests/hil-fixture-test-plan.md)
- [Releasing](../docs/releasing.md)
