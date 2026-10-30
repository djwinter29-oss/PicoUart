#!/usr/bin/env sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
set -eu

PYTHON_EXE="${PYTHON_EXE:-python3}"

"$PYTHON_EXE" -m py_compile \
    host/python/hatch_build.py \
    host/python/src/pico_uart/*.py \
    host/python/src/pico_uart/web/*.py \
    tools/hil/src/hil_test_suite/*.py \
    tools/hil/src/hil_test_suite/workflows/*.py \
    tools/hil/src/hil_test_suite/serial/*.py \
    tools/hil/src/hil_test_suite/support/*.py \
    tools/hil/tests/*.py \
    tools/validation/filter_lock_exclude.py \
    tools/release/check-usb-identity.py \
    tools/release/verify-build.py