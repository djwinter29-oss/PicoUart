#!/usr/bin/env bash
set -euo pipefail

HIL_PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
exec uv run --project "$HIL_PROJECT_DIR" --extra test pico-uart-hil full "$@"