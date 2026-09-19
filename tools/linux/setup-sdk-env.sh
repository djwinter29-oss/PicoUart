#!/usr/bin/env sh

SCRIPT_PATH="${BASH_SOURCE:-$0}"
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$SCRIPT_PATH")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)

if [ ! -f "$REPO_ROOT/firmware/CMakeLists.txt" ]; then
    REPO_ROOT=$(pwd)
fi

PICO_SDK_PATH_VALUE="$REPO_ROOT/.pico-sdk"
PICO_SDK_REPOSITORY="https://github.com/raspberrypi/pico-sdk.git"
PICO_SDK_TARGET_VERSION="${PICO_SDK_VERSION:-}"
PICO_SDK_VERSION_PINNED=0

if [ -n "$PICO_SDK_TARGET_VERSION" ]; then
    PICO_SDK_VERSION_PINNED=1
fi

while [ "$#" -gt 0 ]; do
    case "$1" in
        --sdk-version)
            PICO_SDK_TARGET_VERSION="$2"
            PICO_SDK_VERSION_PINNED=1
            shift 2
            ;;
        *)
            echo "Unknown argument: $1" >&2
            return 1 2>/dev/null || exit 1
            ;;
    esac
done

if ! command -v git >/dev/null 2>&1; then
    echo "git is required to download or update the Pico SDK." >&2
    return 1 2>/dev/null || exit 1
fi

if [ "$PICO_SDK_VERSION_PINNED" -eq 0 ]; then
    echo "Checking for the latest Pico SDK release"
    PICO_SDK_TARGET_VERSION=$(git ls-remote --tags --refs "$PICO_SDK_REPOSITORY" 'refs/tags/[0-9]*' |
        awk -F/ '{print $3}' |
        grep -E '^[0-9]+\.[0-9]+\.[0-9]+$' |
        sort -V |
        tail -n 1)
    if [ -z "$PICO_SDK_TARGET_VERSION" ]; then
        echo "Unable to determine the latest Pico SDK release." >&2
        return 1 2>/dev/null || exit 1
    fi
fi

if [ ! -d "$PICO_SDK_PATH_VALUE" ]; then
    echo "Downloading Pico SDK $PICO_SDK_TARGET_VERSION into $PICO_SDK_PATH_VALUE"
    if ! git clone --branch "$PICO_SDK_TARGET_VERSION" --depth 1 --recurse-submodules \
        "$PICO_SDK_REPOSITORY" "$PICO_SDK_PATH_VALUE"; then
        echo "Pico SDK download failed." >&2
        return 1 2>/dev/null || exit 1
    fi
elif [ "$PICO_SDK_VERSION_PINNED" -eq 0 ]; then
    if ! git -C "$PICO_SDK_PATH_VALUE" rev-parse --git-dir >/dev/null 2>&1; then
        echo "Pico SDK is not a git checkout: $PICO_SDK_PATH_VALUE" >&2
        return 1 2>/dev/null || exit 1
    fi

    if ! git -C "$PICO_SDK_PATH_VALUE" fetch --depth 1 --force origin \
        "refs/tags/$PICO_SDK_TARGET_VERSION:refs/tags/$PICO_SDK_TARGET_VERSION"; then
        echo "Pico SDK update check failed." >&2
        return 1 2>/dev/null || exit 1
    fi

    CURRENT_SDK_REVISION=$(git -C "$PICO_SDK_PATH_VALUE" rev-parse HEAD 2>/dev/null)
    LATEST_SDK_REVISION=$(git -C "$PICO_SDK_PATH_VALUE" rev-parse "$PICO_SDK_TARGET_VERSION^{commit}" 2>/dev/null)
    if [ "$CURRENT_SDK_REVISION" != "$LATEST_SDK_REVISION" ]; then
        echo "Updating Pico SDK checkout to $PICO_SDK_TARGET_VERSION"
        if ! git -C "$PICO_SDK_PATH_VALUE" checkout --detach "$PICO_SDK_TARGET_VERSION"; then
            echo "Pico SDK checkout update failed." >&2
            return 1 2>/dev/null || exit 1
        fi
    else
        echo "Pico SDK is already at $PICO_SDK_TARGET_VERSION"
    fi

    if ! git -C "$PICO_SDK_PATH_VALUE" submodule update --init --recursive; then
        echo "Pico SDK submodule update failed." >&2
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