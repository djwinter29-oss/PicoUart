#!/usr/bin/env sh
set -eu

BUILD_DIR="${BUILD_DIR:-firmware/build}"
GENERATOR="${GENERATOR:-}"
PICO_SDK_PATH_VALUE="${PICO_SDK_PATH:-}"
SKIP_BUILD=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --generator)
            GENERATOR="$2"
            shift 2
            ;;
        --pico-sdk-path)
            PICO_SDK_PATH_VALUE="$2"
            shift 2
            ;;
        --skip-build)
            SKIP_BUILD=1
            shift
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
BUILD_DIR_PATH="$REPO_ROOT/$BUILD_DIR"

if [ "$SKIP_BUILD" -eq 0 ]; then
    BUILD_DIR="$BUILD_DIR" GENERATOR="$GENERATOR" PICO_SDK_PATH="$PICO_SDK_PATH_VALUE" "$SCRIPT_DIR/build.sh"
fi

if [ -f "$BUILD_DIR_PATH/CTestTestfile.cmake" ]; then
    ctest --test-dir "$BUILD_DIR_PATH" --output-on-failure
else
    echo "No CMake tests are configured for this repository."
fi