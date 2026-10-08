#!/usr/bin/env sh
set -eu

PYTHON_EXE="${PYTHON_EXE:-python3}"
SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIR/../.." && pwd)

"$PYTHON_EXE" -c "import hid; print('hidapi OK')"
PYTHONPATH="$REPO_ROOT/host/python/src${PYTHONPATH:+:$PYTHONPATH}" \
	"$PYTHON_EXE" -m pico_uart --help >/dev/null
"$REPO_ROOT/tools/hil/runner/bridge.sh" --help >/dev/null
"$REPO_ROOT/tools/hil/runner/stress.sh" --help >/dev/null