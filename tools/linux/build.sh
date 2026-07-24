#!/usr/bin/env sh
set -eu

BUILD_DIR="${BUILD_DIR:-build/firmware}"
BOARD="${PICO_BOARD:-}"
GENERATOR="${GENERATOR:-}"
PICO_SDK_PATH_VALUE=""
FIRMWARE_VERSION="${PICO_UART_VERSION:-}"
SYSTEM_CLOCK_KHZ=""

while [ "$#" -gt 0 ]; do
    case "$1" in
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --board)
            BOARD="$2"
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
        --firmware-version)
            FIRMWARE_VERSION="$2"
            shift 2
            ;;
        --system-clock-khz)
            SYSTEM_CLOCK_KHZ="$2"
            shift 2
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

if [ -n "$BOARD" ]; then
    if [ "$BUILD_DIR" = "build/firmware" ]; then
        BUILD_DIR="build/firmware-$BOARD"
    fi
fi

if [ -n "$SYSTEM_CLOCK_KHZ" ] && ! printf '%s' "$SYSTEM_CLOCK_KHZ" | grep -Eq '^[0-9]+$'; then
    echo "System clock must be an integer kHz value." >&2
    exit 1
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
SOURCE_DIR="$REPO_ROOT/firmware"
BUILD_DIR_PATH="$REPO_ROOT/$BUILD_DIR"

if [ -z "$PICO_SDK_PATH_VALUE" ]; then
    PICO_SDK_PATH_VALUE="$REPO_ROOT/.pico-sdk"
fi

if [ ! -f "$PICO_SDK_PATH_VALUE/external/pico_sdk_import.cmake" ]; then
    echo "Pico SDK is not available at $PICO_SDK_PATH_VALUE. Run . tools/linux/setup-sdk-env.sh first." >&2
    exit 1
fi

if [ -f "$BUILD_DIR_PATH/CMakeCache.txt" ] &&
    ! grep -F -q "CMAKE_TOOLCHAIN_FILE:FILEPATH=$PICO_SDK_PATH_VALUE/" "$BUILD_DIR_PATH/CMakeCache.txt"; then
    echo "Pico SDK changed; resetting generated CMake state in $BUILD_DIR_PATH"
    rm -rf "$BUILD_DIR_PATH/CMakeCache.txt" \
           "$BUILD_DIR_PATH/CMakeFiles" \
           "$BUILD_DIR_PATH/build.ninja" \
           "$BUILD_DIR_PATH/cmake_install.cmake" \
           "$BUILD_DIR_PATH/Makefile" \
           "$BUILD_DIR_PATH/_deps" \
           "$BUILD_DIR_PATH/pico-sdk" \
           "$BUILD_DIR_PATH/pioasm"
fi

if [ -z "$GENERATOR" ]; then
    if command -v ninja >/dev/null 2>&1; then
        GENERATOR="Ninja"
    else
        GENERATOR="Unix Makefiles"
    fi
fi

set -- \
    -S "$SOURCE_DIR" \
    -B "$BUILD_DIR_PATH" \
    -G "$GENERATOR" \
    -DPICO_SDK_PATH="$PICO_SDK_PATH_VALUE"

if [ -n "$FIRMWARE_VERSION" ]; then
    set -- "$@" -DPICO_UART_VERSION="$FIRMWARE_VERSION"
fi

if [ -n "$SYSTEM_CLOCK_KHZ" ]; then
    set -- "$@" -DPICO_UART_SYSTEM_CLOCK_KHZ="$SYSTEM_CLOCK_KHZ"
fi

if [ -n "$BOARD" ]; then
    set -- "$@" -DPICO_BOARD="$BOARD"
fi

cmake "$@"
cmake --build "$BUILD_DIR_PATH" --parallel