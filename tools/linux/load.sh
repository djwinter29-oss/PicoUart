#!/usr/bin/env sh
set -eu

BUILD_DIR="${BUILD_DIR:-firmware/build}"
BOARD="${PICO_BOARD:-}"
UF2_PATH=""
MOUNT_PATH="${PICO_MOUNT_PATH:-}"
GENERATOR="${GENERATOR:-}"
PICO_SDK_PATH_VALUE="${PICO_SDK_PATH:-}"
SKIP_BUILD=0

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
        --uf2)
            UF2_PATH="$2"
            shift 2
            ;;
        --mount-path)
            MOUNT_PATH="$2"
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

if [ -n "$BOARD" ]; then
    case "$BOARD" in
        pico|pico2)
            ;;
        *)
            echo "Unsupported board '$BOARD'. Use 'pico' or 'pico2'." >&2
            exit 1
            ;;
    esac

    if [ "$BUILD_DIR" = "firmware/build" ]; then
        BUILD_DIR="firmware/build-$BOARD"
    fi
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
BUILD_DIR_PATH="$REPO_ROOT/$BUILD_DIR"

if [ "$SKIP_BUILD" -eq 0 ]; then
    BUILD_DIR="$BUILD_DIR" PICO_BOARD="$BOARD" GENERATOR="$GENERATOR" PICO_SDK_PATH="$PICO_SDK_PATH_VALUE" "$SCRIPT_DIR/build.sh"
fi

if [ -z "$UF2_PATH" ]; then
    UF2_PATH="$BUILD_DIR_PATH/pico_uart.uf2"
fi

if [ ! -f "$UF2_PATH" ]; then
    echo "UF2 file not found: $UF2_PATH" >&2
    exit 1
fi

if command -v picotool >/dev/null 2>&1; then
    picotool load "$UF2_PATH" -f
    picotool reboot
    exit 0
fi

if [ -z "$MOUNT_PATH" ]; then
    for candidate in \
        "/media/$USER/RPI-RP2" \
        "/run/media/$USER/RPI-RP2" \
        "/Volumes/RPI-RP2"
    do
        if [ -d "$candidate" ]; then
            MOUNT_PATH="$candidate"
            break
        fi
    done
fi

if [ -z "$MOUNT_PATH" ] || [ ! -d "$MOUNT_PATH" ]; then
    echo "Could not find the RPI-RP2 mount point. Use --mount-path or install picotool." >&2
    exit 1
fi

cp "$UF2_PATH" "$MOUNT_PATH/pico_uart.uf2"