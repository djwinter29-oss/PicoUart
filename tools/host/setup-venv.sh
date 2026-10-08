#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
PROJECT_DIR="$REPO_ROOT/host/python"
LOCK_FILE="$PROJECT_DIR/requirements-lock.txt"
VENV_DIR="$PROJECT_DIR/.venv"
PYTHON="${PYTHON:-python3}"

usage() {
	cat <<'EOF'
Usage: setup-venv.sh [--venv-dir PATH]

Create the PicoUart host Python environment from the hash-locked requirements.
Relative --venv-dir paths are resolved from the current working directory.
Set PYTHON to select the Python 3.10+ interpreter.
EOF
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--venv-dir)
			if [[ $# -lt 2 ]]; then
				usage >&2
				exit 2
			fi
			VENV_DIR="$2"
			shift 2
			;;
		-h|--help)
			usage
			exit 0
			;;
		*)
			printf 'Unknown argument: %s\n' "$1" >&2
			usage >&2
			exit 2
			;;
	esac
done

if [[ "$VENV_DIR" == "~" ]]; then
	VENV_DIR="$HOME"
elif [[ "$VENV_DIR" == "~/"* ]]; then
	VENV_DIR="$HOME/${VENV_DIR#~/}"
fi
if [[ "$VENV_DIR" != /* ]]; then
	VENV_DIR="$PWD/$VENV_DIR"
fi
mkdir -p -- "$(dirname -- "$VENV_DIR")"
VENV_DIR="$(cd -- "$(dirname -- "$VENV_DIR")" && pwd)/$(basename -- "$VENV_DIR")"

if ! "$PYTHON" -c 'import sys; raise SystemExit(sys.version_info < (3, 10))'; then
	printf 'PicoUart host tools require Python 3.10 or newer (%s)\n' "$PYTHON" >&2
	exit 1
fi

"$PYTHON" -m venv "$VENV_DIR"
VENV_PYTHON="$VENV_DIR/bin/python"
"$VENV_PYTHON" -m pip install --require-hashes -r "$LOCK_FILE"
"$VENV_PYTHON" -m pip install --no-deps --no-build-isolation -e "$PROJECT_DIR"

printf 'Environment ready: %s\n' "$VENV_DIR"
printf 'Activate with: source %s/bin/activate\n' "$VENV_DIR"
printf 'Check the CLI with: %s -m pico_uart --help\n' "$VENV_PYTHON"