#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
VENV_DIR="${VENV_DIR:-$REPO_ROOT/host/python/.venv}"
BASE_PYTHON="${PYTHON:-python3}"

case "${1:-}" in
    -h|--help)
        cat <<'EOF'
Usage: tools/host/run-python.sh [application arguments...]

Run the Python host; defaults to web at http://127.0.0.1:5000/.
Creates a virtualenv and installs locked runtime dependencies when needed.
Set PYTHON to select Python 3.10+, or VENV_DIR to select the environment.
Use status --help, web --help, etc. for application options.
EOF
        exit 0
        ;;
esac

"$SCRIPT_DIR/hid-permissions-check.sh"

if [ ! -x "$VENV_DIR/bin/python" ]; then
    if ! command -v "$BASE_PYTHON" >/dev/null 2>&1; then
        echo "Python interpreter not found: $BASE_PYTHON" >&2
        exit 1
    fi
    "$BASE_PYTHON" -c 'import sys; raise SystemExit(sys.version_info < (3, 10))' || {
        echo "PicoUart requires Python 3.10 or newer." >&2
        exit 1
    }
    "$BASE_PYTHON" -m venv "$VENV_DIR"
fi

PYTHON_EXE="$VENV_DIR/bin/python"
if ! "$PYTHON_EXE" -c 'import flask, hid' >/dev/null 2>&1; then
    PIP_CONFIG_FILE=/dev/null PIP_INDEX_URL=https://pypi.org/simple PIP_EXTRA_INDEX_URL= \
        "$PYTHON_EXE" -m pip install --require-hashes -r "$REPO_ROOT/host/python/requirements-runtime-lock.txt"
fi

if [ "$#" -eq 0 ]; then
    set -- web
fi
PYTHONPATH="$REPO_ROOT/host/python/src${PYTHONPATH:+:$PYTHONPATH}" \
    exec "$PYTHON_EXE" -m pico_uart "$@"
