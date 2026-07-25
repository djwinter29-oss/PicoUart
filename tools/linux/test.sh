#!/usr/bin/env sh
set -eu

BUILD_DIR="${BUILD_DIR:-build/firmware}"
GENERATOR="${GENERATOR:-}"
PICO_SDK_PATH_VALUE=""
SKIP_BUILD=0
SKIP_HOST=0

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
        --skip-host)
            SKIP_HOST=1
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

if [ -z "$PICO_SDK_PATH_VALUE" ]; then
    if [ -n "${PICO_SDK_PATH:-}" ]; then
        PICO_SDK_PATH_VALUE="$PICO_SDK_PATH"
    else
        PICO_SDK_PATH_VALUE="$REPO_ROOT/.pico-sdk"
    fi
fi

if [ "$SKIP_BUILD" -eq 0 ]; then
    BUILD_DIR="$BUILD_DIR" GENERATOR="$GENERATOR" \
        "$SCRIPT_DIR/build.sh" --pico-sdk-path "$PICO_SDK_PATH_VALUE"
fi

if [ "$SKIP_HOST" -eq 0 ]; then
    GENERATOR="$GENERATOR" "$SCRIPT_DIR/test-host.sh"
fi
