#!/usr/bin/env sh
set -eu

PYTHON_EXE="${PYTHON_EXE:-python3}"

"$PYTHON_EXE" -c "import hid; print('hidapi OK')"
"$PYTHON_EXE" host/python/src/pico_uart_hid.py --help >/dev/null
"$PYTHON_EXE" tools/hardware/serial_bridge_test.py --help >/dev/null
"$PYTHON_EXE" tools/hardware/serial_stress_benchmark.py --help >/dev/null