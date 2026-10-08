#!/usr/bin/env sh
set -eu

PYTHON_EXE="${PYTHON_EXE:-python3}"

"$PYTHON_EXE" -m py_compile \
    host/python/src/pico_uart/*.py \
    host/python/src/pico_uart/web/*.py \
    tools/hardware/serial_bridge_test.py \
    tools/hardware/serial_stress_benchmark.py \
    tools/hardware/run_functional_test.py \
    tools/hardware/run_performance_test.py \
    tools/hardware/run_hardware_test.py \
    tools/hardware/hardware_test_result.py \
    tools/hardware/hardware_test_health.py \
    tools/validation/filter_lock_exclude.py \
    tools/release/check-usb-identity.py \
    tools/release/verify-build.py