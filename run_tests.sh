#!/usr/bin/env bash
#
# Builds and runs the SparkySIEM unit tests inside a Linux container.
#
# The monitors rely on inotify, which only exists on Linux, so this is the way to run
# the suite from macOS or Windows. On a Linux host you can just run `make test`.
#
# Any arguments are passed through to the test binary, for example:
#   ./run_tests.sh --gtest_filter='FileMonitorRotation.*'
#
set -euo pipefail

IMAGE_NAME="sparkysiem-tests"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

docker build -q -f "${REPO_ROOT}/Dockerfile.test" -t "${IMAGE_NAME}" "${REPO_ROOT}" > /dev/null

# The repository is mounted read only and all build output goes to a scratch path in
# the container, so running the tests never leaves artifacts in the working tree.
docker run --rm \
    -v "${REPO_ROOT}:/work:ro" \
    -w /work \
    "${IMAGE_NAME}" \
    make test BUILD_DIR=/tmp/sparky-build GTEST_ARGS="$*"
