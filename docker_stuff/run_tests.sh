#!/usr/bin/env bash
#
# Builds SparkySIEM and runs its unit tests inside a Linux container.
#
# The project depends on inotify, so it cannot be built natively on macOS or
# Windows; this script is the supported way to build and test from those hosts.
# It also works on Linux, where it keeps the toolchain reproducible.
#
# Usage:
#   ./docker_stuff/run_tests.sh                # build and run every test
#   ./docker_stuff/run_tests.sh --gtest_filter='FileMonitorTailing.*'
#
# Any arguments are forwarded to the test binary.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="sparky-siem-test:latest"
BUILD_DIR="build/docker"

echo "==> Building test image ${IMAGE}"
docker build -q -t "${IMAGE}" -f "${REPO_ROOT}/docker_stuff/Dockerfile.test" \
    "${REPO_ROOT}/docker_stuff" >/dev/null

echo "==> Configuring and building in ${BUILD_DIR}"
# The build directory lives under the mounted repo so results persist between
# runs and CLion can browse them.
docker run --rm \
    -v "${REPO_ROOT}:/work" \
    -w /work \
    "${IMAGE}" \
    bash -euo pipefail -c "
        cmake -S . -B '${BUILD_DIR}' -DCMAKE_BUILD_TYPE=Debug >/dev/null
        cmake --build '${BUILD_DIR}' --parallel \"\$(nproc)\"
    "

echo "==> Running tests"
if [ "$#" -gt 0 ]; then
    docker run --rm -v "${REPO_ROOT}:/work" -w /work "${IMAGE}" \
        "./${BUILD_DIR}/tests/sparky_tests" "$@"
else
    docker run --rm -v "${REPO_ROOT}:/work" -w "/work/${BUILD_DIR}" "${IMAGE}" \
        ctest --output-on-failure
fi
