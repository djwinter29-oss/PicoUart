# Repository Tools

The `tools/` directory contains local build, test, hardware, and release helpers.
Use the root [README](../README.md) as the project documentation index; this
file only maps the tool categories.

## Categories

| Directory | Purpose |
| --- | --- |
| `firmware/` | Pico SDK setup, firmware builds, and Debug Probe loading |
| `hardware/` | Physical HIL runners and serial bridge/stress tools |
| `release/` | Release version, USB identity, and artifact verification |
| `test/` | Host tests, coverage, syntax checks, static analysis, and smoke checks |

## Common Commands

```sh
# Firmware
. tools/firmware/setup-sdk-env.sh --sdk-version 2.3.0
tools/firmware/build.sh --board pico
tools/firmware/build.sh --board pico2

# Host tests
tools/test/test-host.sh
tools/test/check.sh --skip-build

# Hardware test help
python3 tools/hardware/run_hardware_test.py --help

# Release artifact verification
python3 tools/release/verify-build.py --help
```

## Canonical Documentation

- [Project README](../README.md)
- [Firmware build and configuration](../firmware/build-and-config.md)
- [Functional Test Plan](../docs/tests/functional-test-plan.md)
- [Performance Test Plan](../docs/tests/performance-test-plan.md)
- [Releasing](../docs/releasing.md)
