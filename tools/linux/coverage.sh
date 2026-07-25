#!/usr/bin/env sh
# Host-side coverage for firmware/tests Unity targets (no Pico board required).
set -eu

BUILD_DIR="${BUILD_DIR:-build/host-coverage}"
OUTPUT_DIR="${OUTPUT_DIR:-build/host-coverage/report}"
GENERATOR="${GENERATOR:-}"
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
        --skip-build)
            SKIP_BUILD=1
            shift
            ;;
        --skip-tests)
            SKIP_TESTS=1
            shift
            ;;
        --pico-sdk-path)
            # Accepted for compatibility with older callers; host tests do not need the SDK.
            shift 2
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

if ! command -v gcovr >/dev/null 2>&1; then
    echo "gcovr is not installed or not on PATH." >&2
    exit 1
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
SOURCE_DIR="$REPO_ROOT/firmware/tests"
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
    # Force GCC: the default /usr/bin/cc may be clang without profile runtime libs.
    COV_CC="${CC:-gcc}"
    COV_CXX="${CXX:-g++}"
    CC="$COV_CC" CXX="$COV_CXX" cmake \
        -S "$SOURCE_DIR" \
        -B "$BUILD_DIR_PATH" \
        -G "$GENERATOR" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_COMPILER="$COV_CC" \
        -DCMAKE_C_FLAGS=--coverage\ -O0\ -g \
        -DCMAKE_EXE_LINKER_FLAGS=--coverage
    cmake --build "$BUILD_DIR_PATH" --parallel
fi

if [ ! -f "$BUILD_DIR_PATH/CTestTestfile.cmake" ]; then
    echo "Host Unity/CTest targets were not generated in $BUILD_DIR_PATH." >&2
    exit 1
fi

if [ "$SKIP_TESTS" -eq 0 ]; then
    ctest --test-dir "$BUILD_DIR_PATH" --output-on-failure
fi

mkdir -p "$OUTPUT_DIR_PATH"
# Drop CMake compiler-id coverage junk that confuses gcovr.
find "$BUILD_DIR_PATH" -path '*/CompilerIdC/*' \( -name '*.gcno' -o -name '*.gcda' \) -delete 2>/dev/null || true

run_gcovr() {
    gcovr \
        --root "$REPO_ROOT" \
        --object-directory "$BUILD_DIR_PATH" \
        --filter 'firmware/src/uart/ring_buffer' \
        --filter 'firmware/src/uart/line_coding.c' \
        --exclude '.*CMakeFiles/.*' \
        --exclude-unreachable-branches \
        --exclude-throw-branches \
        --gcov-ignore-errors=no_working_dir_found \
        "$@"
}

run_gcovr --print-summary
run_gcovr --html-details "$HTML_REPORT"
run_gcovr --xml-pretty -o "$XML_REPORT"

echo "Coverage reports written to: $OUTPUT_DIR_PATH"
