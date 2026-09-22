#!/usr/bin/env sh

SCRIPT_PATH="${BASH_SOURCE:-$0}"
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$SCRIPT_PATH")" && pwd)
REPO_ROOT="${PICO_UART_REPO_ROOT:-$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)}"

if [ ! -f "$REPO_ROOT/firmware/CMakeLists.txt" ]; then
    echo "Repository root not found from $SCRIPT_DIR; run this script from a PicoUart checkout." >&2
    return 1 2>/dev/null || exit 1
fi

PICO_SDK_PATH_VALUE="$REPO_ROOT/.pico-sdk"
PICO_SDK_REPOSITORY="https://github.com/raspberrypi/pico-sdk.git"
PICO_SDK_TARGET_VERSION="${PICO_SDK_VERSION:-2.3.0}"
PICO_SDK_TARGET_REVISION="${PICO_SDK_REVISION:-98a542c1a62fb549ffb5d66a3e5892b06276b670}"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --sdk-version)
            PICO_SDK_TARGET_VERSION="$2"
            shift 2
            ;;
        --sdk-revision)
            PICO_SDK_TARGET_REVISION="$2"
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

if [ -z "$PICO_SDK_TARGET_VERSION" ]; then
    echo "Pico SDK version must not be empty." >&2
    return 1 2>/dev/null || exit 1
fi

if [ ! -d "$PICO_SDK_PATH_VALUE" ]; then
    echo "Downloading Pico SDK $PICO_SDK_TARGET_VERSION into $PICO_SDK_PATH_VALUE"
    if ! git clone --branch "$PICO_SDK_TARGET_VERSION" --depth 1 --recurse-submodules \
        "$PICO_SDK_REPOSITORY" "$PICO_SDK_PATH_VALUE"; then
        echo "Pico SDK download failed." >&2
        return 1 2>/dev/null || exit 1
    fi
else
    if ! git -C "$PICO_SDK_PATH_VALUE" rev-parse --git-dir >/dev/null 2>&1; then
        echo "Pico SDK is not a git checkout: $PICO_SDK_PATH_VALUE" >&2
        return 1 2>/dev/null || exit 1
    fi

    if ! git -C "$PICO_SDK_PATH_VALUE" fetch --depth 1 --force origin \
        "refs/tags/$PICO_SDK_TARGET_VERSION:refs/tags/$PICO_SDK_TARGET_VERSION"; then
        echo "Pico SDK update check failed." >&2
        return 1 2>/dev/null || exit 1
    fi

    TAG_SDK_REVISION=$(git -C "$PICO_SDK_PATH_VALUE" rev-parse "$PICO_SDK_TARGET_VERSION^{commit}" 2>/dev/null)
    if [ -z "$TAG_SDK_REVISION" ]; then
        echo "Unable to resolve Pico SDK $PICO_SDK_TARGET_VERSION." >&2
        return 1 2>/dev/null || exit 1
    fi
    if [ "$TAG_SDK_REVISION" != "$PICO_SDK_TARGET_REVISION" ]; then
        echo "Pico SDK tag $PICO_SDK_TARGET_VERSION resolved to $TAG_SDK_REVISION, expected $PICO_SDK_TARGET_REVISION." >&2
        return 1 2>/dev/null || exit 1
    fi
    CURRENT_SDK_REVISION=$(git -C "$PICO_SDK_PATH_VALUE" rev-parse HEAD 2>/dev/null)
    if [ "$CURRENT_SDK_REVISION" != "$PICO_SDK_TARGET_REVISION" ]; then
        echo "Updating Pico SDK checkout to $PICO_SDK_TARGET_REVISION ($PICO_SDK_TARGET_VERSION)"
        if ! git -C "$PICO_SDK_PATH_VALUE" checkout --detach "$PICO_SDK_TARGET_REVISION"; then
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

CURRENT_SDK_REVISION=$(git -C "$PICO_SDK_PATH_VALUE" rev-parse HEAD 2>/dev/null)
if [ "$CURRENT_SDK_REVISION" != "$PICO_SDK_TARGET_REVISION" ]; then
    echo "Pico SDK revision mismatch: expected $PICO_SDK_TARGET_REVISION, got $CURRENT_SDK_REVISION" >&2
    return 1 2>/dev/null || exit 1
fi
if [ -n "$(git -C "$PICO_SDK_PATH_VALUE" status --porcelain --untracked-files=no)" ]; then
    echo "Pico SDK checkout or submodules contain tracked modifications." >&2
    return 1 2>/dev/null || exit 1
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
echo "Pico SDK $PICO_SDK_TARGET_VERSION revision: $CURRENT_SDK_REVISION"
