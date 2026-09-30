#!/usr/bin/env sh
# Run host-side automated tests (native C Unity + Python pytest). No Pico board required.
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
HOST_TEST_BUILD_DIR="${HOST_TEST_BUILD_DIR:-$REPO_ROOT/build/host-tests}"
GENERATOR="${GENERATOR:-}"
PYTHON_EXE="${PYTHON_EXE:-}"
SKIP_C=0
SKIP_PYTHON=0
SANITIZE=0

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
        --sanitize)
            SANITIZE=1
            shift
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

if [ -z "$PYTHON_EXE" ]; then
    if command -v python3 >/dev/null 2>&1; then
        PYTHON_EXE="python3"
    else
        PYTHON_EXE="python"
    fi
fi

if [ -z "$GENERATOR" ]; then
    if command -v ninja >/dev/null 2>&1; then
        GENERATOR="Ninja"
    else
        GENERATOR="Unix Makefiles"
    fi
fi

if [ "$SKIP_C" -eq 0 ]; then
    echo "=== Host C unit tests (Unity / CTest) ==="
    if [ "$SANITIZE" -ne 0 ]; then
        if [ "$HOST_TEST_BUILD_DIR" = "$REPO_ROOT/build/host-tests" ]; then
            HOST_TEST_BUILD_DIR="$REPO_ROOT/build/host-tests-asan"
        fi
        echo "ASan/UBSan enabled in $HOST_TEST_BUILD_DIR"
        cmake -S "$REPO_ROOT/firmware/tests" -B "$HOST_TEST_BUILD_DIR" -G "$GENERATOR" \
            -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
            -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
    else
        cmake -S "$REPO_ROOT/firmware/tests" -B "$HOST_TEST_BUILD_DIR" -G "$GENERATOR"
    fi
    cmake --build "$HOST_TEST_BUILD_DIR" --parallel
    ctest --test-dir "$HOST_TEST_BUILD_DIR" --output-on-failure
fi

if [ "$SKIP_PYTHON" -eq 0 ]; then
    if ! "$PYTHON_EXE" -m pip --version >/dev/null 2>&1; then
        if [ -x "$REPO_ROOT/.venv/bin/python" ]; then
            PYTHON_EXE="$REPO_ROOT/.venv/bin/python"
        fi
    fi

    if ! "$PYTHON_EXE" -m pip --version >/dev/null 2>&1; then
        echo "Python pip is unavailable for '$PYTHON_EXE' and no usable repo virtualenv was found." >&2
        echo "Install python3-venv, create .venv, and rerun, or use --skip-python for C tests only." >&2
        exit 1
    fi

    echo "=== Host Python tests (pytest) ==="
    LOCK_FILE="$REPO_ROOT/host/python/requirements-lock.txt"
    if ! "$PYTHON_EXE" -m pip install -q --require-hashes -r "$LOCK_FILE"; then
        # hidapi is a native extension with no prebuilt wheel for every Python
        # build (for example a very new CPython); its sdist build then fails.
        # ponytail: retry once after any install failure; pip has no stable
        # machine-readable cause here. Never ignore failure of the hashed
        # fallback install. Add structured cause detection if pip supports it.
        # HID-dependent tests skip only when hidapi is actually unavailable.
        echo "Warning: full requirements-lock.txt install failed; retrying without hidapi" >&2
        echo "so non-HID host tests can still run. HID tests skip if hidapi is unavailable." >&2
        NO_HIDAPI_LOCK_FILE=$(mktemp)
        # Single cleanup trap for the rest of the script's life: it fires once
        # at actual process exit (rm -f is safe to repeat), so nothing here
        # ever needs to clear or reinstall a trap.
        trap 'rm -f "$NO_HIDAPI_LOCK_FILE"' EXIT HUP INT TERM
        awk -v exclude=hidapi -f "$SCRIPT_DIR/filter-lock-exclude.awk" \
            "$LOCK_FILE" > "$NO_HIDAPI_LOCK_FILE"
        ORIGINAL_PACKAGE_COUNT=$(awk '/^[[:alnum:]_.-]+==/ { count++ } END { print count+0 }' "$LOCK_FILE")
        FILTERED_PACKAGE_COUNT=$(awk '/^[[:alnum:]_.-]+==/ { count++ } END { print count+0 }' "$NO_HIDAPI_LOCK_FILE")
        if [ "$FILTERED_PACKAGE_COUNT" -ge "$ORIGINAL_PACKAGE_COUNT" ] \
            || grep -q '^hidapi==' "$NO_HIDAPI_LOCK_FILE" \
            || grep -q '# via hidapi' "$NO_HIDAPI_LOCK_FILE"; then
            echo "Could not safely exclude hidapi from the locked requirements; refusing partial install." >&2
            exit 1
        fi
        "$PYTHON_EXE" -m pip install -q --require-hashes -r "$NO_HIDAPI_LOCK_FILE"
    fi
    (
        CDPATH= cd -- "$REPO_ROOT"
        "$PYTHON_EXE" -m pytest -c host/python/pyproject.toml
    )
fi
