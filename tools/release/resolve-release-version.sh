#!/usr/bin/env sh
# Resolve and validate the release version string used by
# .github/workflows/release.yml. Extracted to a standalone script so the
# release-tag policy (plain MAJOR.MINOR.PATCH, major/minor 0-99, patch 0-255)
# can be exercised by host tests without running the workflow itself.
#
# Usage: resolve-release-version.sh <event_name> <input_version> <ref_name>
# Prints the resolved "MAJOR.MINOR.PATCH" version to stdout on success.
# Exits non-zero with a message on stderr for any policy violation.
set -eu

EVENT_NAME="${1:?event name required}"
INPUT_VERSION="${2:-}"
REF_NAME="${3:-}"

if [ "$EVENT_NAME" = "workflow_dispatch" ]; then
    VERSION="$INPUT_VERSION"
else
    VERSION="${REF_NAME#v}"
fi

if ! printf '%s' "$VERSION" | grep -Eq '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$'; then
    echo "Refusing version '$VERSION' (expected plain MAJOR.MINOR.PATCH like 1.2.3)" >&2
    exit 1
fi

OLD_IFS=$IFS
IFS=.
set -- $VERSION
IFS=$OLD_IFS

if [ "$1" -gt 99 ] || [ "$2" -gt 99 ] || [ "$3" -gt 255 ]; then
    # This 0-99 major/minor cap is a release-tag policy, not a firmware
    # build limit: local builds via tools/firmware/build.sh accept major/minor/patch
    # up to 255 (see firmware/CMakeLists.txt), falling back to USB
    # bcdDevice 0x0000 above 99.
    echo "Refusing version '$VERSION' (major/minor must be 0-99; patch must be 0-255)" >&2
    exit 1
fi

echo "$VERSION"
