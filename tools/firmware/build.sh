#!/usr/bin/env sh
set -eu

BUILD_DIR="${BUILD_DIR:-build/firmware}"
BOARD="${PICO_BOARD:-pico}"
GENERATOR="${GENERATOR:-}"
PICO_SDK_PATH_VALUE=""
FIRMWARE_VERSION="${PICO_UART_VERSION:-}"
ALLOW_HID_RESET="${PICO_UART_ALLOW_HID_RESET:-OFF}"

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
        --system-clock-khz|--unsafe-overclock)
            echo "Build-time clocks were removed; use tools/firmware/config.py to select a stored profile." >&2
            exit 1
            ;;
        --allow-hid-reset)
            ALLOW_HID_RESET="ON"
            shift
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

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIR/../.." && pwd)
SOURCE_DIR="$REPO_ROOT/firmware"
case "$BUILD_DIR" in
    /*) BUILD_DIR_PATH="$BUILD_DIR" ;;
    *) BUILD_DIR_PATH="$REPO_ROOT/$BUILD_DIR" ;;
esac

if [ -z "$PICO_SDK_PATH_VALUE" ]; then
    # Prefer an explicit CLI path, then a sourced/exported PICO_SDK_PATH, then the
    # project-local checkout created by setup-sdk-env.sh.
    if [ -n "${PICO_SDK_PATH:-}" ]; then
        PICO_SDK_PATH_VALUE="$PICO_SDK_PATH"
    else
        PICO_SDK_PATH_VALUE="$REPO_ROOT/.pico-sdk"
    fi
fi

if [ ! -f "$PICO_SDK_PATH_VALUE/external/pico_sdk_import.cmake" ]; then
    echo "Pico SDK is not available at $PICO_SDK_PATH_VALUE. Run . tools/firmware/setup-sdk-env.sh first." >&2
    exit 1
fi

if [ -z "$GENERATOR" ]; then
    if command -v ninja >/dev/null 2>&1; then
        GENERATOR="Ninja"
    else
        GENERATOR="Unix Makefiles"
    fi
fi

if [ -f "$BUILD_DIR_PATH/CMakeCache.txt" ] &&
        { ! grep -F -q "CMAKE_TOOLCHAIN_FILE:FILEPATH=$PICO_SDK_PATH_VALUE/" "$BUILD_DIR_PATH/CMakeCache.txt" ||
            ! grep -F -q "CMAKE_GENERATOR:INTERNAL=$GENERATOR" "$BUILD_DIR_PATH/CMakeCache.txt" ||
            ! grep -F -q "PICO_BOARD:STRING=$BOARD" "$BUILD_DIR_PATH/CMakeCache.txt" ||
            ! grep -F -q "PICO_UART_VERSION:STRING=${FIRMWARE_VERSION:-0.0.0-dev}" "$BUILD_DIR_PATH/CMakeCache.txt" ||
            grep -q '^PICO_UART_SYSTEM_CLOCK_KHZ:' "$BUILD_DIR_PATH/CMakeCache.txt" ||
            ! grep -F -q "PICO_UART_ALLOW_HID_RESET:BOOL=$ALLOW_HID_RESET" "$BUILD_DIR_PATH/CMakeCache.txt" ||
            { [ -f "$BUILD_DIR_PATH/picotool/src/picotoolBuild-stamp/picotoolBuild-configure" ] &&
                [ ! -f "$BUILD_DIR_PATH/_deps/picotool-build/CMakeCache.txt" ]; }; }; then
        echo "Build configuration changed; resetting generated CMake state in $BUILD_DIR_PATH"
    rm -rf "$BUILD_DIR_PATH/CMakeCache.txt" \
           "$BUILD_DIR_PATH/CMakeFiles" \
           "$BUILD_DIR_PATH/build.ninja" \
           "$BUILD_DIR_PATH/cmake_install.cmake" \
           "$BUILD_DIR_PATH/Makefile" \
           "$BUILD_DIR_PATH/_deps" \
           "$BUILD_DIR_PATH/pico-sdk" \
           "$BUILD_DIR_PATH/picotool" \
           "$BUILD_DIR_PATH/pioasm"
fi

set -- \
    -S "$SOURCE_DIR" \
    -B "$BUILD_DIR_PATH" \
    -G "$GENERATOR" \
    -DPICO_SDK_PATH="$PICO_SDK_PATH_VALUE" \
    -DPICO_BOARD="$BOARD" \
    -DPICO_UART_VERSION="${FIRMWARE_VERSION:-0.0.0-dev}" \
    -DPICO_UART_ALLOW_HID_RESET="$ALLOW_HID_RESET"

cmake "$@"
cmake --build "$BUILD_DIR_PATH" --parallel
