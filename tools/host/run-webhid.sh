#!/usr/bin/env sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
PYTHON_EXE="${PYTHON:-python3}"
PORT=8000

while [ "$#" -gt 0 ]; do
    case "$1" in
        --port)
            if [ "$#" -lt 2 ]; then
                echo "--port requires a port number." >&2
                exit 2
            fi
            PORT="$2"
            shift 2
            ;;
        -h|--help)
            cat <<'EOF'
Usage: tools/host/run-webhid.sh [--port PORT]

Serve the browser-only WebHID dashboard at http://127.0.0.1:8000/.
Set PYTHON to select the Python interpreter; no packages or virtualenv required.
USB access is local to the browser's computer.
EOF
            exit 0
            ;;
        *)
            echo "Unknown argument: $1; use --help." >&2
            exit 2
            ;;
    esac
done

case "$PORT" in
    ''|*[!0-9]*)
        echo "Port must be an integer from 1 to 65535." >&2
        exit 2
        ;;
esac
if [ "${#PORT}" -gt 5 ] || [ "$PORT" -eq 0 ] || [ "$PORT" -gt 65535 ]; then
    echo "Port must be an integer from 1 to 65535." >&2
    exit 2
fi
if ! command -v "$PYTHON_EXE" >/dev/null 2>&1; then
    echo "Python interpreter not found: $PYTHON_EXE" >&2
    exit 1
fi
exec "$PYTHON_EXE" -m http.server "$PORT" --bind 127.0.0.1 --directory "$REPO_ROOT/host/webhid"
