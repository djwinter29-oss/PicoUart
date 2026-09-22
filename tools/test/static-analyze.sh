#!/usr/bin/env sh
set -eu

if ! command -v cppcheck >/dev/null 2>&1; then
    echo "cppcheck is not installed or not on PATH." >&2
    exit 1
fi

cppcheck --error-exitcode=1 --enable=warning --inline-suppr \
    -I firmware/src \
    --suppress=missingIncludeSystem \
    --suppress=missingInclude \
    firmware/src/uart/backend_policy.h \
    firmware/src/uart/control_pending.h \
    firmware/src/uart/dma_progress_math.h \
    firmware/src/uart/hw/baud_rate.h \
    firmware/src/uart/line_coding.c \
    firmware/src/board/uart_topology.c \
    firmware/src/uart/hw/dma_claim.c \
    firmware/src/uart/hw/driver.c \
    firmware/src/uart/hw/dma_claim_ops.c \
    firmware/src/uart/hw/dma_claim.h \
    firmware/src/uart/pio/resource_claim.c \
    firmware/src/uart/pio/driver.c \
    firmware/src/uart/pio/resource_claim_ops.c \
    firmware/src/uart/pio/resource_claim.h \
    firmware/src/uart/pio/txstall_wait.h \
    firmware/src/usb/cdc_soft_pending.h