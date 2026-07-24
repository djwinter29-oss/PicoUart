#!/usr/bin/env sh
set -eu

BUILD_DIR="${BUILD_DIR:-build/firmware}"
BOARD="${PICO_BOARD:-}"
ELF_PATH=""
GENERATOR="${GENERATOR:-}"
PICO_SDK_PATH_VALUE=""
SKIP_BUILD=0
SYSTEM_CLOCK_KHZ=""
OPENOCD_EXE="${OPENOCD_EXE:-openocd}"
OPENOCD_TARGET="${PICO_OPENOCD_TARGET:-}"
ADAPTER_SPEED_KHZ="${PICO_DEBUG_PROBE_SPEED_KHZ:-5000}"

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
        --elf)
            ELF_PATH="$2"
            shift 2
            ;;
        --openocd-exe)
            OPENOCD_EXE="$2"
            shift 2
            ;;
        --openocd-target)
            OPENOCD_TARGET="$2"
            shift 2
            ;;
        --adapter-speed-khz)
            ADAPTER_SPEED_KHZ="$2"
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

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
BUILD_DIR_PATH="$REPO_ROOT/$BUILD_DIR"

if [ -z "$PICO_SDK_PATH_VALUE" ]; then
    PICO_SDK_PATH_VALUE="$REPO_ROOT/.pico-sdk"
fi

if [ "$SKIP_BUILD" -eq 0 ]; then
    if [ -n "$SYSTEM_CLOCK_KHZ" ]; then
        BUILD_DIR="$BUILD_DIR" PICO_BOARD="$BOARD" GENERATOR="$GENERATOR" PICO_SDK_PATH="$PICO_SDK_PATH_VALUE" \
            "$SCRIPT_DIR/build.sh" --system-clock-khz "$SYSTEM_CLOCK_KHZ"
    else
        BUILD_DIR="$BUILD_DIR" PICO_BOARD="$BOARD" GENERATOR="$GENERATOR" PICO_SDK_PATH="$PICO_SDK_PATH_VALUE" "$SCRIPT_DIR/build.sh"
    fi
fi

if [ -z "$ELF_PATH" ]; then
    ELF_PATH="$BUILD_DIR_PATH/pico_uart.elf"
fi

if [ ! -f "$ELF_PATH" ]; then
    echo "ELF file not found: $ELF_PATH" >&2
    exit 1
fi

if [ -z "$OPENOCD_TARGET" ]; then
    case "${BOARD:-pico}" in
        pico|pico_w|rp2040*)
            OPENOCD_TARGET="target/rp2040.cfg"
            ;;
        pico2|pico2_w|rp2350*)
            OPENOCD_TARGET="target/rp2350.cfg"
            ;;
        *)
            echo "No default OpenOCD target for board '$BOARD'. Use --openocd-target." >&2
            exit 1
            ;;
    esac
fi

if ! command -v "$OPENOCD_EXE" >/dev/null 2>&1; then
    echo "OpenOCD executable not found: $OPENOCD_EXE" >&2
    exit 1
fi

"$OPENOCD_EXE" \
    -f interface/cmsis-dap.cfg \
    -f "$OPENOCD_TARGET" \
    -c "adapter speed $ADAPTER_SPEED_KHZ" \
    -c "program $ELF_PATH verify reset exit"