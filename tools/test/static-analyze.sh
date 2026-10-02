#!/usr/bin/env sh
set -eu

if ! command -v cppcheck >/dev/null 2>&1; then
    echo "cppcheck is not installed or not on PATH." >&2
    exit 1
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
CDPATH= cd -- "$REPO_ROOT"

# Scan every firmware translation unit, plus standalone policy headers.
cppcheck --error-exitcode=1 --enable=warning --inline-suppr \
    -D__isr= \
    -I firmware/src \
    -I firmware/src/config \
    --suppress=missingIncludeSystem \
    --suppress=missingInclude \
    firmware/src \
    firmware/src/uart/backend/policy.h \
    firmware/src/uart/control/ownership.h \
    firmware/src/uart/dma/progress_math.h \
    firmware/src/uart/hw/baud_rate.h \
    firmware/src/uart/hw/dma_claim.h \
    firmware/src/uart/pio/resource_claim.h \
    firmware/src/uart/pio/txstall_wait.h \
    firmware/src/usb/cdc_soft_pending.h