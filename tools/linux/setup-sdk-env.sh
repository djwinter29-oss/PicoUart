#!/usr/bin/env sh

SCRIPT_PATH="${BASH_SOURCE:-$0}"
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$SCRIPT_PATH")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)

if [ ! -f "$REPO_ROOT/firmware/CMakeLists.txt" ]; then
    REPO_ROOT=$(pwd)
fi

PICO_SDK_PATH_VALUE="$REPO_ROOT/.pico-sdk"
PICO_SDK_VERSION="${PICO_SDK_VERSION:-2.2.0}"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --sdk-version)
            PICO_SDK_VERSION="$2"
            shift 2
            ;;
        *)
            echo "Unknown argument: $1" >&2
            return 1 2>/dev/null || exit 1
            ;;
    esac
done

if [ ! -d "$PICO_SDK_PATH_VALUE" ]; then
    if ! command -v git >/dev/null 2>&1; then
        echo "git is required to download the Pico SDK." >&2
        return 1 2>/dev/null || exit 1
    fi

    echo "Downloading Pico SDK $PICO_SDK_VERSION into $PICO_SDK_PATH_VALUE"
    if ! git clone --branch "$PICO_SDK_VERSION" --depth 1 --recurse-submodules \
        https://github.com/raspberrypi/pico-sdk.git "$PICO_SDK_PATH_VALUE"; then
        echo "Pico SDK download failed." >&2
        return 1 2>/dev/null || exit 1
    fi
fi

if [ ! -f "$PICO_SDK_PATH_VALUE/external/pico_sdk_import.cmake" ]; then
    echo "Pico SDK is incomplete: $PICO_SDK_PATH_VALUE" >&2
    return 1 2>/dev/null || exit 1
fi

if ! command -v arm-none-eabi-gcc >/dev/null 2>&1; then
    echo "arm-none-eabi-gcc is not available on PATH." >&2
    return 1 2>/dev/null || exit 1
fi

export PICO_SDK_PATH="$PICO_SDK_PATH_VALUE"
echo "PICO_SDK_PATH=$PICO_SDK_PATH"