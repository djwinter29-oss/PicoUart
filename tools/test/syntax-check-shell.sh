#!/usr/bin/env sh
set -eu

for script in \
    tools/firmware/build.sh \
    tools/firmware/load.sh \
    tools/firmware/setup-sdk-env.sh \
    tools/release/resolve-release-version.sh \
    tools/test/check.sh \
    tools/test/coverage-firmware-c.sh \
    tools/test/smoke-host-tools.sh \
    tools/test/static-analyze.sh \
    tools/test/syntax-check-python.sh \
    tools/test/test-host.sh; do
    sh -n "$script"
done