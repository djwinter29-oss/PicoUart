#!/usr/bin/env sh
set -eu

PYTHON_EXE="${PYTHON_EXE:-python3}"
SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIR/../.." && pwd)

"$PYTHON_EXE" -c "import hid; print('hidapi OK')"
PYTHONPATH="$REPO_ROOT/host/python/src${PYTHONPATH:+:$PYTHONPATH}" \
	"$PYTHON_EXE" -m pico_uart --help >/dev/null
"$PYTHON_EXE" "$REPO_ROOT/tools/hardware/serial_bridge_test.py" --help >/dev/null
"$PYTHON_EXE" "$REPO_ROOT/tools/hardware/serial_stress_benchmark.py" --help >/dev/null