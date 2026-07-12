#!/usr/bin/env sh
set -eu

BUILD_DIR="${BUILD_DIR:-firmware/build-coverage}"
OUTPUT_DIR="${OUTPUT_DIR:-firmware/build-coverage/coverage}"
GENERATOR="${GENERATOR:-}"
PICO_SDK_PATH_VALUE="${PICO_SDK_PATH:-}"
SKIP_BUILD=0
SKIP_TESTS=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --output-dir)
            OUTPUT_DIR="$2"
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
        --skip-tests)
            SKIP_TESTS=1
            shift
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

if [ -z "$PICO_SDK_PATH_VALUE" ]; then
    echo "PICO_SDK_PATH is not set." >&2
    exit 1
fi

if ! command -v gcovr >/dev/null 2>&1; then
    echo "gcovr is not installed or not on PATH." >&2
    exit 1
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
SOURCE_DIR="$REPO_ROOT/firmware"
BUILD_DIR_PATH="$REPO_ROOT/$BUILD_DIR"
OUTPUT_DIR_PATH="$REPO_ROOT/$OUTPUT_DIR"
HTML_REPORT="$OUTPUT_DIR_PATH/index.html"
XML_REPORT="$OUTPUT_DIR_PATH/coverage.xml"

if [ -z "$GENERATOR" ]; then
    if command -v ninja >/dev/null 2>&1; then
        GENERATOR="Ninja"
    else
        GENERATOR="Unix Makefiles"
    fi
fi

if [ "$SKIP_BUILD" -eq 0 ]; then
    cmake \
        -S "$SOURCE_DIR" \
        -B "$BUILD_DIR_PATH" \
        -G "$GENERATOR" \
        -DPICO_SDK_PATH="$PICO_SDK_PATH_VALUE" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_FLAGS=--coverage\ -O0\ -g \
        -DCMAKE_CXX_FLAGS=--coverage\ -O0\ -g \
        -DCMAKE_EXE_LINKER_FLAGS=--coverage
    cmake --build "$BUILD_DIR_PATH" --parallel
fi

if [ ! -f "$BUILD_DIR_PATH/CTestTestfile.cmake" ]; then
    echo "No CMake tests are configured for this repository, so no coverage report can be generated yet." >&2
    exit 1
fi

if [ "$SKIP_TESTS" -eq 0 ]; then
    ctest --test-dir "$BUILD_DIR_PATH" --output-on-failure
fi

mkdir -p "$OUTPUT_DIR_PATH"

gcovr \
    --root "$REPO_ROOT" \
    --object-directory "$BUILD_DIR_PATH" \
    --filter 'firmware/src' \
    --exclude-unreachable-branches \
    --exclude-throw-branches \
    --print-summary

gcovr \
    --root "$REPO_ROOT" \
    --object-directory "$BUILD_DIR_PATH" \
    --filter 'firmware/src' \
    --exclude-unreachable-branches \
    --exclude-throw-branches \
    --html-details "$HTML_REPORT"

gcovr \
    --root "$REPO_ROOT" \
    --object-directory "$BUILD_DIR_PATH" \
    --filter 'firmware/src' \
    --exclude-unreachable-branches \
    --exclude-throw-branches \
    --xml-pretty "$XML_REPORT"

echo "Coverage reports written to: $OUTPUT_DIR_PATH"