#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
set -euo pipefail

HIL_PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
HIL_CLI="$HIL_PROJECT_DIR/.venv/bin/pico-uart-hil"
if [[ ! -x "$HIL_CLI" ]]; then
	printf 'HIL environment missing; run tools/hil/setup.sh first.\n' >&2
	exit 2
fi
exec "$HIL_CLI" full "$@"