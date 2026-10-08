#!/usr/bin/env sh
set -eu

PYTHON_EXE="${PYTHON_EXE:-python3}"

"$PYTHON_EXE" -m py_compile \
    host/python/src/pico_uart/*.py \
    host/python/src/pico_uart/web/*.py \
    tools/hil/src/hil_test_suite/*.py \
    tools/hil/tests/*.py \
    tools/validation/filter_lock_exclude.py \
    tools/release/check-usb-identity.py \
    tools/release/verify-build.py