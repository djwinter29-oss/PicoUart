#!/usr/bin/env sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
DOTNET_EXE="${DOTNET:-dotnet}"

case "${1:-}" in
    -h|--help)
        cat <<'EOF'
Usage: tools/host/run-dotnet.sh [application arguments...]

Run the .NET host; defaults to web at http://127.0.0.1:5000/.
Requires the .NET 10 SDK. Set DOTNET to select its executable.
Use run-dotnet.sh help for application options.
EOF
        exit 0
        ;;
esac

"$SCRIPT_DIR/hid-permissions-check.sh"

if ! command -v "$DOTNET_EXE" >/dev/null 2>&1; then
    echo ".NET SDK executable not found: $DOTNET_EXE; install .NET 10 or set DOTNET." >&2
    exit 1
fi
if [ "$#" -eq 0 ]; then
    set -- web
fi
exec "$DOTNET_EXE" run --project "$REPO_ROOT/host/dotnet/PicoUart" --no-launch-profile -- "$@"
