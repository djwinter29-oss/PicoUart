#!/usr/bin/env sh
# Run host-side automated tests (native C Unity + Python pytest). No Pico board required.
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
HOST_TEST_BUILD_DIR="${HOST_TEST_BUILD_DIR:-$REPO_ROOT/build/host-tests}"
GENERATOR="${GENERATOR:-}"
PYTHON_EXE="${PYTHON_EXE:-python3}"
SKIP_C=0
SKIP_PYTHON=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        --build-dir)
            HOST_TEST_BUILD_DIR="$2"
            shift 2
            ;;
        --generator)
            GENERATOR="$2"
            shift 2
            ;;
        --skip-c)
            SKIP_C=1
            shift
            ;;
        --skip-python)
            SKIP_PYTHON=1
            shift
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

if [ -z "$GENERATOR" ]; then
    if command -v ninja >/dev/null 2>&1; then
        GENERATOR="Ninja"
    else
        GENERATOR="Unix Makefiles"
    fi
fi

if [ "$SKIP_PYTHON" -eq 0 ] && ! "$PYTHON_EXE" -m pip --version >/dev/null 2>&1; then
    if [ -x "$REPO_ROOT/.venv/bin/python" ]; then
        PYTHON_EXE="$REPO_ROOT/.venv/bin/python"
    else
        echo "Python pip is unavailable for '$PYTHON_EXE' and no repo virtualenv was found." >&2
        exit 1
    fi
fi

if [ "$SKIP_C" -eq 0 ]; then
    echo "=== Host C unit tests (Unity / CTest) ==="
    cmake -S "$REPO_ROOT/firmware/tests" -B "$HOST_TEST_BUILD_DIR" -G "$GENERATOR"
    cmake --build "$HOST_TEST_BUILD_DIR" --parallel
    ctest --test-dir "$HOST_TEST_BUILD_DIR" --output-on-failure
fi

if [ "$SKIP_PYTHON" -eq 0 ]; then
    echo "=== Host Python tests (pytest) ==="
    "$PYTHON_EXE" -m pip install -q -r "$REPO_ROOT/host/python/requirements-dev.txt"
    (
        CDPATH= cd -- "$REPO_ROOT"
        "$PYTHON_EXE" -m pytest
    )
fi
