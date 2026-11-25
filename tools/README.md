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

`tools/host/run-python.sh`, `run-dotnet.sh`, and `run-direct.sh` start a dashboard from a checkout. For ports, multiple
instances, and environment overrides, see [Launch Scripts](../docs/development/host-tools.md#launch-scripts).

## Linux HID Permissions

Run both commands from the account that will use the host application:

```sh
tools/host/hid-permissions-setup.sh
tools/host/hid-permissions-check.sh
```

For setup behavior and troubleshooting, see the
[Linux HID permissions guide](../docs/hardware-setup.md#linux-hid-permissions).
