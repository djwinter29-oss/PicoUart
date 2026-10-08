# Repository Tools

The `tools/` directory contains local build, test, hardware, and release helpers. Use the root [README](../README.md) as
the project documentation index; this file only maps the tool categories.

## Categories

| Directory     | Purpose                                                                                          |
| ------------- | ------------------------------------------------------------------------------------------------ |
| `firmware/`   | Pico SDK setup, firmware builds, and Debug Probe loading                                         |
| `hil/`        | Reusable Python project for physical HIL runners and serial bridge/stress tools                  |
| `host/`       | Host Python environment setup                                                                    |
| `release/`    | Release version, USB identity, and artifact verification                                         |
| `validation/` | Test runners, lock filtering, coverage helpers, syntax checks, static analysis, and smoke checks |

## Common Commands

```sh
# Firmware
. tools/firmware/setup-sdk-env.sh --sdk-version 2.3.0
tools/firmware/build.sh --board pico
tools/firmware/build.sh --board pico2

# Host tests
# Linux/macOS
tools/host/setup-venv.sh
# Windows PowerShell
.\tools\host\setup-venv.ps1
tools/validation/run-host-tests.sh
tools/validation/validate.sh --skip-build

# Hardware test project
uv sync --project tools/hil --extra test
uv run --project tools/hil --extra test pytest tools/hil/tests
tools/hil/runner/full.sh --help

# Release artifact verification
python3 tools/release/verify-build.py --help
```

## Canonical Documentation

- [Project README](../README.md)
- [Firmware build and configuration](../firmware/build-and-config.md)
- [HIL Fixture Test Plan](../docs/tests/hil-fixture-test-plan.md)
- [Releasing](../docs/releasing.md)
