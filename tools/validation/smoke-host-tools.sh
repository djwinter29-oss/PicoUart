#!/usr/bin/env sh
set -eu

PYTHON_EXE="${PYTHON_EXE:-python3}"
SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIR/../.." && pwd)

"$PYTHON_EXE" -c "import hid; print('hidapi OK')"
PYTHONPATH="$REPO_ROOT/host/python/src${PYTHONPATH:+:$PYTHONPATH}" \
	"$PYTHON_EXE" -m pico_uart --help >/dev/null
PYTHONPATH="$REPO_ROOT/host/python/src:$REPO_ROOT/tools/hil/src${PYTHONPATH:+:$PYTHONPATH}" \
	"$PYTHON_EXE" -m hil_test_suite.serial.bridge --help >/dev/null
PYTHONPATH="$REPO_ROOT/host/python/src:$REPO_ROOT/tools/hil/src${PYTHONPATH:+:$PYTHONPATH}" \
	"$PYTHON_EXE" -m hil_test_suite.serial.stress --help >/dev/null