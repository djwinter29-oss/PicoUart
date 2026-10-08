#!/usr/bin/env sh
set -eu

for script in \
    tools/firmware/build.sh \
    tools/firmware/load.sh \
    tools/firmware/setup-sdk-env.sh \
    tools/release/resolve-release-version.sh \
    tools/validation/validate.sh \
    tools/validation/coverage-firmware-c.sh \
    tools/validation/smoke-host-tools.sh \
    tools/validation/static-analyze.sh \
    tools/validation/syntax-check-python.sh \
    tools/validation/run-host-tests.sh; do
    sh -n "$script"
done