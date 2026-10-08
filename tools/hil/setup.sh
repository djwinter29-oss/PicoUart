#!/usr/bin/env bash
set -euo pipefail

HIL_PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$HIL_PROJECT_DIR/../.." && pwd)"
VENV_DIR="$HIL_PROJECT_DIR/.venv"
PYTHON_VERSION="${PYTHON_VERSION:-3.12}"

uv venv --python "$PYTHON_VERSION" "$VENV_DIR"
uv pip install --python "$VENV_DIR/bin/python" --editable "$REPO_ROOT/host/python"
uv pip install --python "$VENV_DIR/bin/python" --editable "$HIL_PROJECT_DIR[test]"