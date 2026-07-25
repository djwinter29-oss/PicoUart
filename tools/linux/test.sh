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
BUILD_DIR_PATH="$REPO_ROOT/$BUILD_DIR"

if [ -z "$PICO_SDK_PATH_VALUE" ]; then
    PICO_SDK_PATH_VALUE="$REPO_ROOT/.pico-sdk"
fi

if [ "$SKIP_BUILD" -eq 0 ]; then
    BUILD_DIR="$BUILD_DIR" GENERATOR="$GENERATOR" PICO_SDK_PATH="$PICO_SDK_PATH_VALUE" "$SCRIPT_DIR/build.sh"
fi

if [ "$SKIP_HOST" -eq 0 ]; then
    GENERATOR="$GENERATOR" "$SCRIPT_DIR/test-host.sh"
fi
